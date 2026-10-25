/*
 * jsonb_slice.c
 *
 * Read one top-level object field from an external, uncompressed jsonb value.
 * Slice only an uncompressed value stored in a TOAST table. Inline data is
 * already in the row, and compressed data needs complete decompression.
 *
 * Slice offsets start at the logical JSONB data, after the outer varlena
 * header. A root object with N key-value pairs has this layout:
 *
 *   logical offset 0
 *   +------------------------------------+
 *   | container header                   | type and pair count
 *   +------------------------------------+
 *   | key JEntries   [0 .. N - 1]        | type and length/end offset
 *   | value JEntries [N .. 2 * N - 1]    | type and length/end offset
 *   +------------------------------------+
 *   | all key bytes                      |
 *   | all value bytes                    | padding before some values
 *   +------------------------------------+
 *
 * JEntry positions are relative to the data area after the directory. Null
 * and Boolean values have no data bytes. Compressed TOAST bytes do not keep
 * these logical positions, so compressed input uses the complete-value path.
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

/* A JSONB container starts with one uint32 that stores its type and count. */
#define JSONB_CONTAINER_HEADER_SIZE ((int32)sizeof(uint32))

/* Last successful sliced call in this backend; exposed only to the tests. */
static int64 last_fetched_bytes = -1;

/* Carries one external value and its safe read boundary through the helpers. */
typedef struct SliceSource
{
	/* Raw external datum used for each slice request. */
	struct varlena *attr;
	/* Logical byte size used to reject out-of-range requests. */
	int32 total_size;
	/* Test-only sum of requested slice sizes. */
	int64 fetched_bytes;
} SliceSource;

/* Identifies one JSONB container inside the complete logical value. */
typedef struct ContainerLocation
{
	/* Absolute offset of the container header. */
	int32 start;
	/* Byte size of the container, including its header and directory. */
	int32 size;
	/* Only the root can use JSONB's raw-scalar array wrapper. */
	bool root;
} ContainerLocation;

/* Keeps decoded value metadata separate from fetching and rebuilding it. */
typedef struct ValueLocation
{
	/* JEntry type bits. */
	uint32 type;
	/* Absolute slice offset in the JSONB data. */
	int32 start;
	/* Byte size to request; zero for null and Boolean values. */
	int32 size;
} ValueLocation;

/*
 * Read bytes [start, start + size) from the external value. Check every
 * range here so callers never trust JSONB metadata without a boundary check.
 * The returned varlena owns the bytes and must be freed by the caller.
 */
static struct varlena *
slice_fetch(SliceSource *source, int64 start, int64 size)
{
	struct varlena *slice;

	if (start < 0 || size < 0 || start > source->total_size ||
		size > source->total_size - start || size > INT_MAX)
		return NULL;

	slice = detoast_attr_slice(source->attr, (int32)start, (int32)size);
	if (VARSIZE_ANY_EXHDR(slice) != (Size)size)
	{
		pfree(slice);
		return NULL;
	}
	source->fetched_bytes += size;
	return slice;
}

/* Initialize a SliceSource with the given external datum. */
static bool
slice_source_init(struct varlena *attr, SliceSource *source)
{
	struct varatt_external toast_pointer;

	/* Only external, uncompressed values can be sliced. */
	if (!VARATT_IS_EXTERNAL_ONDISK(attr))
		return false;
	/* Check if the external datum is compressed. */
	VARATT_EXTERNAL_GET_POINTER(toast_pointer, attr);
	if (VARATT_EXTERNAL_IS_COMPRESSED(toast_pointer))
		return false;

	source->attr = attr;
	source->total_size = VARATT_EXTERNAL_GET_EXTSIZE(toast_pointer);
	source->fetched_bytes = 0;

	/* A JSONB root needs at least its four-byte container header. */
	if (source->total_size < JSONB_CONTAINER_HEADER_SIZE)
		return false;
	return true;
}

/* Read one container header and prove that its directory fits. */
static bool
read_container_header(SliceSource *source, const ContainerLocation *container,
					  uint32 *header)
{
	struct varlena *header_slice;
	uint32 header_value;
	uint32 item_count;
	int64 entry_count;
	int64 required_size;
	int64 container_end;

	/* First prove that the complete container is inside the JSONB value. */
	container_end = (int64)container->start + container->size;
	if (container->start < 0 ||
		container->size < JSONB_CONTAINER_HEADER_SIZE ||
		container_end > source->total_size)
		return false;

	/* The first four container bytes store its type and item count. */
	header_slice =
		slice_fetch(source, container->start, JSONB_CONTAINER_HEADER_SIZE);
	if (header_slice == NULL)
		return false;
	memcpy(&header_value, VARDATA_ANY(header_slice),
		   JSONB_CONTAINER_HEADER_SIZE);
	pfree(header_slice);
	item_count = header_value & JB_CMASK;

	if (header_value & JB_FOBJECT)
	{
		/* An object has one key and one value JEntry for each pair. */
		if (header_value & (JB_FARRAY | JB_FSCALAR))
			return false;
		entry_count = 2 * (int64)item_count;
	}
	else if (header_value & JB_FARRAY)
	{
		/* Only the root may wrap one raw scalar as an array. */
		if ((header_value & JB_FSCALAR) &&
			(!container->root || item_count != 1))
			return false;
		entry_count = item_count;
	}
	else
		return false;

	/* The header and all JEntries must fit inside this container. */
	required_size =
		JSONB_CONTAINER_HEADER_SIZE + entry_count * (int64)sizeof(JEntry);
	if (required_size > container->size)
		return false;

	*header = header_value;
	return true;
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
			uint32 *size)
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
	*size = (uint32)(end - start);
	return true;
}

/* Convert a relative value range and skip required alignment padding. */
static bool
locate_value(JEntry entry, int64 data_start, uint32 offset, uint32 size,
			 ValueLocation *location)
{
	uint32 type = entry & JENTRY_TYPEMASK;
	uint32 padding;

	switch (type)
	{
	case JENTRY_ISSTRING:
		location->start = (int32)(data_start + offset);
		location->size = (int32)size;
		break;
	case JENTRY_ISNUMERIC:
	case JENTRY_ISCONTAINER:
		padding = INTALIGN(offset) - offset;
		if (padding > size)
			return false;
		location->start = (int32)(data_start + offset + padding);
		location->size = (int32)(size - padding);
		if (type == JENTRY_ISCONTAINER &&
			location->size < JSONB_CONTAINER_HEADER_SIZE)
			return false;
		break;
	case JENTRY_ISNULL:
	case JENTRY_ISBOOL_FALSE:
	case JENTRY_ISBOOL_TRUE:
		location->start = (int32)(data_start + offset);
		location->size = 0;
		break;
	default:
		return false;
	}

	location->type = type;
	return true;
}

/* A numeric result is passed to JsonbValueToJsonb(), which trusts its size. */
static bool
numeric_is_valid(const char *data, int32 size)
{
	uint32 header_size;
	uint32 stored_size;
	uint16 numeric_header;

	if (size < 1)
		return false;
	if (VARATT_IS_4B_U(data))
	{
		if (size < VARHDRSZ)
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

	if (stored_size != (uint32)size ||
		size < (int32)(header_size + sizeof(uint16)))
		return false;
	memcpy(&numeric_header, data + header_size, sizeof(uint16));
	if ((numeric_header & 0x8000) == 0 &&
		size < (int32)(header_size + 2 * sizeof(uint16)))
		return false;
	return true;
}

/* Turn a located value into the jsonb result of the -> text operator. */
static bool
value_result(SliceSource *source, const ValueLocation *location, Datum *result)
{
	JsonbValue value;
	struct varlena *slice = NULL;

	/* Null and Boolean live in the JEntry, so they need no value slice. */
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
		slice = slice_fetch(source, location->start, location->size);
		if (slice == NULL)
			return false;
		value.type = jbvString;
		value.val.string.val = VARDATA_ANY(slice);
		value.val.string.len = location->size;
		break;
	case JENTRY_ISNUMERIC:
		slice = slice_fetch(source, location->start, location->size);
		if (slice == NULL ||
			!numeric_is_valid(VARDATA_ANY(slice), location->size))
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

		slice = slice_fetch(source, location->start, location->size);
		if (slice == NULL)
			return false;
		jsonb_result = palloc(VARHDRSZ + location->size);
		SET_VARSIZE(jsonb_result, VARHDRSZ + location->size);
		memcpy(VARDATA(jsonb_result), VARDATA_ANY(slice), location->size);
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
 * A JSONB object stores [header][key entries][value entries][keys][values].
 * Read the metadata and keys first. Read the selected value only after a
 * matching key is found. found distinguishes a missing key from invalid data.
 */
static bool
object_lookup(SliceSource *source, const ContainerLocation *container,
			  uint32 pair_count, const char *key, uint32 key_length,
			  ValueLocation *value, bool *found)
{
	struct varlena *directory_slice;
	struct varlena *keys_slice = NULL;
	int64 container_end = (int64)container->start + container->size;
	int64 directory_start =
		(int64)container->start + JSONB_CONTAINER_HEADER_SIZE;
	int64 directory_size;
	int64 data_start;
	uint32 data_size;
	uint32 keys_end;
	const JEntry *entries;
	const char *keys = NULL;
	uint32 low = 0;
	uint32 high;
	bool valid = false;

	*found = false;
	if (pair_count == 0)
		return true;

	directory_size = 2 * (int64)pair_count * (int64)sizeof(JEntry);
	data_start = directory_start + directory_size;
	if (directory_size > INT_MAX || data_start > container_end)
		return false;

	data_size = (uint32)(container_end - data_start);

	/* Request only directory metadata, not key or value payloads. */
	directory_slice = slice_fetch(source, directory_start, directory_size);
	if (directory_slice == NULL)
		return false;
	entries = (const JEntry *)VARDATA_ANY(directory_slice);
	/* The first value entry starts where all key bytes end. */
	if (!entry_offset(entries, pair_count, &keys_end) || keys_end > data_size)
		goto done;
	/* JEntries describe keys; fetch their actual bytes for key comparison. */
	if (keys_end > 0)
	{
		keys_slice = slice_fetch(source, data_start, keys_end);
		if (keys_slice == NULL)
			goto done;
		keys = VARDATA_ANY(keys_slice);
	}

	/* JSONB orders object keys by byte length and then by byte value. */
	high = pair_count;
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
			uint32 value_size;

			/* The value entry is pair_count positions after its key entry. */
			if (!entry_range(entries, pair_count + middle, data_size,
							 &value_offset, &value_size) ||
				!locate_value(entries[pair_count + middle], data_start,
							  value_offset, value_size, value))
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

/*
 * Use PostgreSQL's normal path when slicing cannot help or is not safe.
 * PG_GETARG_JSONB_P detoasts and decompresses the input when required.
 */
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
	/* Keep the raw form so the storage check does not detoast the input. */
	struct varlena *attr = (struct varlena *)PG_GETARG_POINTER(0);
	SliceSource source;
	ContainerLocation root;
	ValueLocation value;
	uint32 pair_count;
	uint32 header;
	text *field_name;
	Datum result = (Datum)0;
	bool found;
	bool valid;

	last_fetched_bytes = -1;

	/* Initialize the slice source. */
	if (!slice_source_init(attr, &source))
		return full_lookup(fcinfo);

	/* Initialize the root container location. */
	root.start = 0;
	root.size = source.total_size;
	root.root = true;

	if (!read_container_header(&source, &root, &header))
		return full_lookup(fcinfo);

	/* A valid non-object has no named field, so report a clean miss. */
	if ((header & JB_FOBJECT) == 0)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}

	field_name = PG_GETARG_TEXT_PP(1);
	pair_count = header & JB_CMASK;
	valid = object_lookup(&source, &root, pair_count, VARDATA_ANY(field_name),
						  VARSIZE_ANY_EXHDR(field_name), &value, &found);

	PG_FREE_IF_COPY(field_name, 1);
	/* Invalid metadata falls back; a valid missing key returns NULL below. */
	if (!valid)
		return full_lookup(fcinfo);
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}

	if (!value_result(&source, &value, &result))
		return full_lookup(fcinfo);

	last_fetched_bytes = source.fetched_bytes;
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
