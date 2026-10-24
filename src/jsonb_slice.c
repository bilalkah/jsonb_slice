/*
 * jsonb_slice.c
 *
 * Read one top-level object field from an external, uncompressed jsonb value.
 * The sliced path fetches the root directory and the selected value through
 * detoast_attr_slice(). Other storage forms use PostgreSQL's normal complete
 * detoast and JSONB lookup.
 */
#include "postgres.h"

#include <limits.h>

#include "access/detoast.h"
#include "access/heaptoast.h"
#include "fmgr.h"
#include "utils/jsonb.h"

#if PG_VERSION_NUM >= 160000
#include "varatt.h"
#endif

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_slice_get);
PG_FUNCTION_INFO_V1(jsonb_slice_test_fetched_bytes);

/* Last successful sliced call in this backend; exposed only to the tests. */
static int64 last_fetched_bytes = -1;

typedef struct SliceSource
{
	struct varlena *attr;
	int32 total;
	int64 fetched_bytes;
} SliceSource;

typedef struct ValueLocation
{
	uint32 type;
	int32 start;
	int32 length;
} ValueLocation;

/*
 * Read bytes [start, start + length) from the external value. The returned
 * varlena owns the bytes and must be freed by the caller.
 */
static struct varlena *
slice_fetch(SliceSource *source, int64 start, int64 length)
{
	struct varlena *slice;

	if (start < 0 || length < 0 || start > source->total ||
		length > source->total - start || length > INT_MAX)
		return NULL;

	slice = detoast_attr_slice(source->attr, (int32)start, (int32)length);
	if (VARSIZE_ANY_EXHDR(slice) != (Size)length)
	{
		pfree(slice);
		return NULL;
	}
	source->fetched_bytes += length;
	return slice;
}

/*
 * Compute an entry's start as getJsonbOffset() does. JSONB stores a complete
 * offset every JB_OFFSET_STRIDE entries and lengths between those offsets.
 */
static bool
entry_offset(const JEntry *entries, uint32 index, uint32 *offset)
{
	uint64 result = 0;
	uint32 i = index;

	while (i > 0)
	{
		JEntry entry = entries[--i];

		result += JBE_OFFLENFLD(entry);
		if (JBE_HAS_OFF(entry))
			break;
	}
	if (result > PG_UINT32_MAX)
		return false;
	*offset = (uint32)result;
	return true;
}

/* Return one entry's byte range inside its container data area. */
static bool
entry_range(const JEntry *entries, uint32 index, uint32 limit, uint32 *offset,
			uint32 *length)
{
	JEntry entry = entries[index];
	uint32 start;
	uint64 end;

	if (!entry_offset(entries, index, &start))
		return false;
	end = JBE_HAS_OFF(entry) ? JBE_OFFLENFLD(entry)
							 : (uint64)start + JBE_OFFLENFLD(entry);
	if (start > end || end > limit)
		return false;

	*offset = start;
	*length = (uint32)(end - start);
	return true;
}

/* Convert a value entry's range into an absolute range in the JSONB datum. */
static bool
locate_value(JEntry entry, int64 data_start, uint32 offset, uint32 length,
			 ValueLocation *location)
{
	uint32 type = entry & JENTRY_TYPEMASK;
	uint32 padding;

	switch (type)
	{
	case JENTRY_ISSTRING:
		location->start = (int32)(data_start + offset);
		location->length = (int32)length;
		break;
	case JENTRY_ISNUMERIC:
	case JENTRY_ISCONTAINER:
		padding = INTALIGN(offset) - offset;
		if (padding > length)
			return false;
		location->start = (int32)(data_start + offset + padding);
		location->length = (int32)(length - padding);
		if (type == JENTRY_ISCONTAINER &&
			location->length < (int32)sizeof(uint32))
			return false;
		break;
	case JENTRY_ISNULL:
	case JENTRY_ISBOOL_FALSE:
	case JENTRY_ISBOOL_TRUE:
		location->start = (int32)(data_start + offset);
		location->length = 0;
		break;
	default:
		return false;
	}

	location->type = type;
	return true;
}

/* A numeric result is passed to JsonbValueToJsonb(), which trusts its size. */
static bool
numeric_is_valid(const char *data, int32 length)
{
	uint32 header_size;
	uint32 stored_size;
	uint16 numeric_header;

	if (length < 1)
		return false;
	if (VARATT_IS_4B_U(data))
	{
		if (length < VARHDRSZ)
			return false;
		header_size = VARHDRSZ;
		stored_size = VARSIZE(data);
	}
	else if (VARATT_IS_1B(data) && !VARATT_IS_1B_E(data))
	{
		header_size = VARHDRSZ_SHORT;
		stored_size = VARSIZE_1B(data);
	}
	else
		return false;

	if (stored_size != (uint32)length ||
		length < (int32)(header_size + sizeof(uint16)))
		return false;
	memcpy(&numeric_header, data + header_size, sizeof(uint16));
	if ((numeric_header & 0x8000) == 0 &&
		length < (int32)(header_size + 2 * sizeof(uint16)))
		return false;
	return true;
}

/* Turn a located value into the jsonb result of the -> text operator. */
static bool
value_result(SliceSource *source, const ValueLocation *location, Datum *result)
{
	JsonbValue value;
	struct varlena *slice = NULL;

	switch (location->type)
	{
	case JENTRY_ISNULL:
		value.type = jbvNull;
		break;
	case JENTRY_ISBOOL_FALSE:
	case JENTRY_ISBOOL_TRUE:
		value.type = jbvBool;
		value.val.boolean = location->type == JENTRY_ISBOOL_TRUE;
		break;
	case JENTRY_ISSTRING:
		slice = slice_fetch(source, location->start, location->length);
		if (slice == NULL)
			return false;
		value.type = jbvString;
		value.val.string.val = VARDATA_ANY(slice);
		value.val.string.len = location->length;
		break;
	case JENTRY_ISNUMERIC:
		slice = slice_fetch(source, location->start, location->length);
		if (slice == NULL ||
			!numeric_is_valid(VARDATA_ANY(slice), location->length))
		{
			if (slice != NULL)
				pfree(slice);
			return false;
		}
		value.type = jbvNumeric;
		value.val.numeric = (Numeric)VARDATA_ANY(slice);
		break;
	case JENTRY_ISCONTAINER:
	{
		Jsonb *jsonb_result;

		slice = slice_fetch(source, location->start, location->length);
		if (slice == NULL)
			return false;
		jsonb_result = palloc(VARHDRSZ + location->length);
		SET_VARSIZE(jsonb_result, VARHDRSZ + location->length);
		memcpy(VARDATA(jsonb_result), VARDATA_ANY(slice), location->length);
		pfree(slice);
		*result = JsonbPGetDatum(jsonb_result);
		return true;
	}
	default:
		return false;
	}

	*result = JsonbPGetDatum(JsonbValueToJsonb(&value));
	if (slice != NULL)
		pfree(slice);
	return true;
}

/*
 * Find key in the root object. found distinguishes a missing key from an
 * invalid byte range, which makes the caller use PostgreSQL's normal lookup.
 */
static bool
slice_lookup(SliceSource *source, const char *key, uint32 key_length,
			 Datum *result, bool *found)
{
	struct varlena *header_slice;
	struct varlena *directory_slice;
	struct varlena *keys_slice = NULL;
	uint32 header;
	uint32 count;
	uint64 directory_length;
	int64 data_start;
	uint32 data_length;
	uint32 keys_end;
	const JEntry *entries;
	const char *keys = NULL;
	uint32 low = 0;
	uint32 high;
	bool valid = false;

	*found = false;
	header_slice = slice_fetch(source, 0, sizeof(uint32));
	if (header_slice == NULL)
		return false;
	memcpy(&header, VARDATA_ANY(header_slice), sizeof(uint32));
	pfree(header_slice);

	if ((header & JB_FOBJECT) == 0)
		return (header & JB_FARRAY) != 0;
	if (header & (JB_FARRAY | JB_FSCALAR))
		return false;

	count = header & JB_CMASK;
	if (count == 0)
		return true;
	directory_length = 2 * (uint64)count * sizeof(JEntry);
	data_start = sizeof(uint32) + directory_length;
	if (directory_length > INT_MAX || data_start > source->total)
		return false;
	data_length = (uint32)(source->total - data_start);

	directory_slice = slice_fetch(source, sizeof(uint32), directory_length);
	if (directory_slice == NULL)
		return false;
	entries = (const JEntry *)VARDATA_ANY(directory_slice);
	if (!entry_offset(entries, count, &keys_end) || keys_end > data_length)
		goto done;
	if (keys_end > 0)
	{
		keys_slice = slice_fetch(source, data_start, keys_end);
		if (keys_slice == NULL)
			goto done;
		keys = VARDATA_ANY(keys_slice);
	}

	high = count;
	while (low < high)
	{
		uint32 middle = low + (high - low) / 2;
		uint32 key_offset;
		uint32 stored_key_length;
		int comparison;

		if (!JBE_ISSTRING(entries[middle]) ||
			!entry_range(entries, middle, keys_end, &key_offset,
						 &stored_key_length))
			goto done;
		if (stored_key_length == 0 && key_length == 0)
			comparison = 0;
		else if (stored_key_length == key_length)
			comparison = memcmp(keys + key_offset, key, key_length);
		else
			comparison = stored_key_length > key_length ? 1 : -1;

		if (comparison == 0)
		{
			uint32 value_offset;
			uint32 value_length;
			ValueLocation location;

			if (!entry_range(entries, count + middle, data_length,
							 &value_offset, &value_length) ||
				!locate_value(entries[count + middle], data_start,
							  value_offset, value_length, &location) ||
				!value_result(source, &location, result))
				goto done;
			*found = true;
			valid = true;
			goto done;
		}
		if (comparison < 0)
			low = middle + 1;
		else
			high = middle;
	}
	valid = true;

done:
	if (keys_slice != NULL)
		pfree(keys_slice);
	pfree(directory_slice);
	return valid;
}

/* Use the complete-detoast implementation from 0.2.0. */
static Datum
full_lookup(FunctionCallInfo fcinfo)
{
	Jsonb *document = PG_GETARG_JSONB_P(0);
	text *field_name = PG_GETARG_TEXT_PP(1);
	JsonbValue *value;
	Jsonb *result;

	if (!JB_ROOT_IS_OBJECT(document))
	{
		PG_FREE_IF_COPY(document, 0);
		PG_FREE_IF_COPY(field_name, 1);
		PG_RETURN_NULL();
	}

	value =
		getKeyJsonValueFromContainer(&document->root, VARDATA_ANY(field_name),
									 VARSIZE_ANY_EXHDR(field_name), NULL);
	if (value == NULL)
	{
		PG_FREE_IF_COPY(document, 0);
		PG_FREE_IF_COPY(field_name, 1);
		PG_RETURN_NULL();
	}

	result = JsonbValueToJsonb(value);
	PG_FREE_IF_COPY(document, 0);
	PG_FREE_IF_COPY(field_name, 1);
	PG_RETURN_JSONB_P(result);
}

Datum
jsonb_slice_get(PG_FUNCTION_ARGS)
{
	struct varlena *attr = (struct varlena *)PG_GETARG_POINTER(0);
	struct varatt_external toast_pointer;
	SliceSource source;
	text *field_name;
	Datum result = (Datum)0;
	bool found;
	bool valid;

	last_fetched_bytes = -1;
	if (!VARATT_IS_EXTERNAL_ONDISK(attr))
		return full_lookup(fcinfo);

	VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);
	if (VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer))
		return full_lookup(fcinfo);
	source.attr = attr;
	source.total = VARATT_EXTERNAL_GET_EXTSIZE(toast_pointer);
	source.fetched_bytes = 0;
	if (source.total < (int32)sizeof(uint32))
		return full_lookup(fcinfo);

	field_name = PG_GETARG_TEXT_PP(1);
	valid = slice_lookup(&source, VARDATA_ANY(field_name),
						 VARSIZE_ANY_EXHDR(field_name), &result, &found);
	PG_FREE_IF_COPY(field_name, 1);
	if (!valid)
		return full_lookup(fcinfo);
	last_fetched_bytes = source.fetched_bytes;
	if (!found)
		PG_RETURN_NULL();
	return result;
}

/* Test support. This function is not installed as an extension member. */
Datum
jsonb_slice_test_fetched_bytes(PG_FUNCTION_ARGS)
{
	if (last_fetched_bytes < 0)
		PG_RETURN_NULL();
	PG_RETURN_INT64(last_fetched_bytes);
}
