/*
 * jsonb_slice.c
 *
 * PostgreSQL entry points for jsonb_slice.
 *
 * The extension currently provides the same top-level reads as these built-in
 * operators:
 *
 *   jsonb_slice_get(jsonb, text)          matches document -> 'key'
 *   jsonb_slice_get(jsonb, integer)       matches document -> index
 *   jsonb_slice_get_text(jsonb, text)     matches document ->> 'key'
 *   jsonb_slice_get_text(jsonb, integer)  matches document ->> index
 *
 * An external, uncompressed JSONB value can use the sliced path in
 * jsonb_slice_core.c. Inline and compressed values use PostgreSQL's built-in
 * complete-value path. A missing key or index returns SQL NULL. The JSONB
 * functions preserve JSON null; the text functions return SQL NULL for it.
 *
 * This file owns SQL arguments, fallback decisions and SQL return values. The
 * core module owns TOAST reads and JSONB layout parsing.
 */
#include "postgres.h"

#include "fmgr.h"
#include "utils/fmgrprotos.h"
#include "utils/jsonb.h"

#include "jsonb_slice_core.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_slice_get);
PG_FUNCTION_INFO_V1(jsonb_slice_get_index);
PG_FUNCTION_INFO_V1(jsonb_slice_get_text_key);
PG_FUNCTION_INFO_V1(jsonb_slice_get_text_index);
PG_FUNCTION_INFO_V1(jsonb_slice_test_fetched_bytes);

/* Last successful sliced call in this backend; exposed only to the tests. */
static int64 last_fetched_bytes = -1;

/* Detoast the complete value and perform the JSONB-returning key lookup. */
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

/*
 * Locate one top-level object field without reading its value payload.
 *
 * Return false when slicing is unavailable or the stored metadata is invalid;
 * the caller must then use PostgreSQL's complete-value path. Return true with
 * found=false for a valid non-object or a missing key. Read the key argument
 * only after the root is known to be an object.
 */
static bool
slice_object_field(FunctionCallInfo fcinfo, SliceSource *source,
				   ValueLocation *value, bool *found)
{
	struct varlena *attr = (struct varlena *)PG_GETARG_POINTER(0);
	ContainerLocation root;
	uint32 pair_count;
	uint32 header;
	text *field_name;
	bool valid;

	*found = false;
	if (!slice_source_init(attr, source))
		return false;

	root.start = 0;
	root.size = source->total_size;
	root.root = true;
	if (!slice_read_container_header(source, &root, &header))
		return false;

	/* A valid non-object has no named field. */
	if ((header & JB_FOBJECT) == 0)
		return true;

	field_name = PG_GETARG_TEXT_PP(1);
	pair_count = header & JB_CMASK;
	valid =
		slice_object_lookup(source, &root, pair_count, VARDATA_ANY(field_name),
							VARSIZE_ANY_EXHDR(field_name), value, found);
	PG_FREE_IF_COPY(field_name, 1);
	return valid;
}

/*
 * Locate one top-level array element without reading its value payload.
 *
 * Return false when the caller must use PostgreSQL's complete-value path.
 * Return true with found=false for a valid non-array or an index outside the
 * array. Negative indexes count from the end, as PostgreSQL does.
 */
static bool
slice_array_element(FunctionCallInfo fcinfo, SliceSource *source,
					ValueLocation *value, bool *found)
{
	struct varlena *attr = (struct varlena *)PG_GETARG_POINTER(0);
	ContainerLocation root;
	int32 requested = PG_GETARG_INT32(1);
	uint32 element_count;
	uint32 index;
	uint32 header;

	*found = false;
	if (!slice_source_init(attr, source))
		return false;

	root.start = 0;
	root.size = source->total_size;
	root.root = true;
	if (!slice_read_container_header(source, &root, &header))
		return false;

	/* A valid non-array has no element at an integer index. */
	if ((header & JB_FARRAY) == 0)
		return true;

	element_count = header & JB_CMASK;
	if (!slice_normalize_array_index(requested, element_count, &index))
		return true;

	return slice_array_lookup(source, &root, element_count, index, value,
							  found);
}

/* Return one top-level object field as JSONB, like the -> text operator. */
Datum
jsonb_slice_get(PG_FUNCTION_ARGS)
{
	SliceSource source;
	ValueLocation value;
	Datum result = (Datum)0;
	bool found;

	last_fetched_bytes = -1;
	if (!slice_object_field(fcinfo, &source, &value, &found))
		return full_lookup(fcinfo);
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}
	if (!slice_value_result(&source, &value, &result))
		return full_lookup(fcinfo);

	last_fetched_bytes = source.fetched_bytes;
	return result;
}

/* Return one top-level object field as SQL text, like the ->> text operator. */
Datum
jsonb_slice_get_text_key(PG_FUNCTION_ARGS)
{
	SliceSource source;
	ValueLocation value;
	Datum result = (Datum)0;
	bool found;
	bool is_null;

	last_fetched_bytes = -1;
	if (!slice_object_field(fcinfo, &source, &value, &found))
		return jsonb_object_field_text(fcinfo);
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}
	if (!slice_value_text_result(&source, &value, &result, &is_null))
		return jsonb_object_field_text(fcinfo);

	last_fetched_bytes = source.fetched_bytes;
	if (is_null)
		PG_RETURN_NULL();
	return result;
}

/* Return one top-level array element as JSONB, like the -> integer operator. */
Datum
jsonb_slice_get_index(PG_FUNCTION_ARGS)
{
	SliceSource source;
	ValueLocation value;
	Datum result = (Datum)0;
	bool found;

	last_fetched_bytes = -1;
	if (!slice_array_element(fcinfo, &source, &value, &found))
		return jsonb_array_element(fcinfo);
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}
	if (!slice_value_result(&source, &value, &result))
		return jsonb_array_element(fcinfo);

	last_fetched_bytes = source.fetched_bytes;
	return result;
}

/* Return one top-level array element as text, like the ->> integer operator. */
Datum
jsonb_slice_get_text_index(PG_FUNCTION_ARGS)
{
	SliceSource source;
	ValueLocation value;
	Datum result = (Datum)0;
	bool found;
	bool is_null;

	last_fetched_bytes = -1;
	if (!slice_array_element(fcinfo, &source, &value, &found))
		return jsonb_array_element_text(fcinfo);
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}
	if (!slice_value_text_result(&source, &value, &result, &is_null))
		return jsonb_array_element_text(fcinfo);

	last_fetched_bytes = source.fetched_bytes;
	if (is_null)
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
