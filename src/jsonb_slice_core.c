/*
 * jsonb_slice_core.c
 *
 * Partial reader for JSONB object fields and array elements. A lookup can use
 * the root container or a nested ContainerLocation. A path read repeats the
 * same lookup operation for each nested container.
 *
 * The sliced path applies only to an external, uncompressed JSONB value. It
 * does not change storage. Inline data is already in the table row, and
 * compressed external data needs complete decompression, so both use the
 * normal PostgreSQL path in jsonb_slice.c.
 *
 * An object lookup inside one container reads:
 *
 *   1. the root container header;
 *   2. the complete JEntry directory;
 *   3. all key bytes in that object, so it can search for the requested key;
 *   4. only the selected value payload.
 *
 * An array lookup inside one container reads the header, directory and
 * selected value payload. It does not need key bytes. Every offset derived
 * from stored JSONB metadata is checked against its container before a slice
 * is requested.
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
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/jsonb.h"

#include "jsonb_slice_core.h"

#if PG_VERSION_NUM >= 160000
#include "varatt.h"
#endif

/* A JSONB container starts with one uint32 that stores its type and count. */
#define JSONB_CONTAINER_HEADER_SIZE ((int32)sizeof(uint32))

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

/* Accept only an on-disk, uncompressed external datum and record its size. */
bool
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

/* Read the four-byte container header and prove that its directory can fit. */
bool
slice_read_container_header(SliceSource *source,
							const ContainerLocation *container, uint32 *header)
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

/* Convert positive or end-relative PostgreSQL indexes to a zero-based index. */
bool
slice_normalize_array_index(int32 requested, uint32 element_count,
							uint32 *index)
{
	int64 from_end;

	if (requested >= 0)
	{
		if ((uint32)requested >= element_count)
			return false;
		*index = (uint32)requested;
		return true;
	}

	/* Cast before negation so INT_MIN is handled in int64. */
	from_end = -(int64)requested;
	if (from_end > element_count)
		return false;
	*index = element_count - (uint32)from_end;
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

/* Fetch the located payload and preserve its JSONB type, including JSON null. */
bool
slice_value_result(SliceSource *source, const ValueLocation *location,
				   Datum *result)
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
 * Fetch and format a text result. Strings lose JSON quotes, containers keep
 * JSON syntax, and JSON null is reported through is_null as SQL NULL.
 */
bool
slice_value_text_result(SliceSource *source, const ValueLocation *location,
						Datum *result, bool *is_null)
{
	struct varlena *slice = NULL;
	char *output;

	*is_null = false;
	switch (location->type)
	{
	case JENTRY_ISNULL:
		*is_null = true;
		return true;
	case JENTRY_ISBOOL_FALSE:
		*result = CStringGetTextDatum("false");
		return true;
	case JENTRY_ISBOOL_TRUE:
		*result = CStringGetTextDatum("true");
		return true;
	case JENTRY_ISSTRING:
		slice = slice_fetch(source, location->start, location->size);
		if (slice == NULL)
			return false;
		*result = PointerGetDatum(
			cstring_to_text_with_len(VARDATA_ANY(slice), location->size));
		pfree(slice);
		return true;
	case JENTRY_ISNUMERIC:
		slice = slice_fetch(source, location->start, location->size);
		if (slice == NULL ||
			!numeric_is_valid(VARDATA_ANY(slice), location->size))
		{
			if (slice != NULL)
				pfree(slice);
			return false;
		}
		output = DatumGetCString(DirectFunctionCall1(
			numeric_out, NumericGetDatum((Numeric)VARDATA_ANY(slice))));
		*result = CStringGetTextDatum(output);
		pfree(output);
		pfree(slice);
		return true;
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
		output = DatumGetCString(
			DirectFunctionCall1(jsonb_out, JsonbPGetDatum(jsonb_result)));
		*result = CStringGetTextDatum(output);
		pfree(output);
		pfree(jsonb_result);
		return true;
	}
	default:
		return false;
	}
}

/*
 * A JSONB object stores [header][key entries][value entries][keys][values].
 * Read the metadata and keys first. Read the selected value only after a
 * matching key is found. found distinguishes a missing key from invalid data.
 */
bool
slice_object_lookup(SliceSource *source, const ContainerLocation *container,
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

/* Read the array directory and locate one element without reading its payload. */
bool
slice_array_lookup(SliceSource *source, const ContainerLocation *container,
				   uint32 element_count, uint32 index, ValueLocation *value,
				   bool *found)
{
	struct varlena *directory_slice;
	int64 container_end = (int64)container->start + container->size;
	int64 directory_start =
		(int64)container->start + JSONB_CONTAINER_HEADER_SIZE;
	int64 directory_size = (int64)element_count * (int64)sizeof(JEntry);
	int64 data_start = directory_start + directory_size;
	uint32 data_size;
	uint32 value_offset;
	uint32 value_size;
	const JEntry *entries;
	bool valid;

	*found = false;
	if (index >= element_count)
		return true;
	if (directory_size > INT_MAX || data_start > container_end)
		return false;
	data_size = (uint32)(container_end - data_start);

	directory_slice = slice_fetch(source, directory_start, directory_size);
	if (directory_slice == NULL)
		return false;
	entries = (const JEntry *)VARDATA_ANY(directory_slice);
	valid =
		entry_range(entries, index, data_size, &value_offset, &value_size) &&
		locate_value(entries[index], data_start, value_offset, value_size,
					 value);
	pfree(directory_slice);
	if (!valid)
		return false;

	*found = true;
	return true;
}
