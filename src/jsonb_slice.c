/*
 * jsonb_slice.c
 *
 * PostgreSQL entry points for jsonb_slice.
 *
 * The extension currently provides the same reads as these built-in operators:
 *
 *   jsonb_slice_get(jsonb, text)          matches document -> 'key'
 *   jsonb_slice_get(jsonb, integer)       matches document -> index
 *   jsonb_slice_get_text(jsonb, text)     matches document ->> 'key'
 *   jsonb_slice_get_text(jsonb, integer)  matches document ->> index
 *
 *   jsonb_slice_path(jsonb, text[])       matches document #> path
 *   jsonb_slice_path_text(jsonb, text[])  matches document #>> path
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

#include <errno.h>
#include <limits.h>
#include <stdlib.h>

#include "catalog/pg_type_d.h"
#include "fmgr.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgrprotos.h"
#include "utils/jsonb.h"

#include "jsonb_slice_core.h"
#include "jsonb_slice_plan.h"

PG_MODULE_MAGIC;

void
_PG_init(void)
{
	jsonb_slice_plan_init();
}

PG_FUNCTION_INFO_V1(jsonb_slice_get);
PG_FUNCTION_INFO_V1(jsonb_slice_get_index);
PG_FUNCTION_INFO_V1(jsonb_slice_get_text_key);
PG_FUNCTION_INFO_V1(jsonb_slice_get_text_index);
PG_FUNCTION_INFO_V1(jsonb_slice_path);
PG_FUNCTION_INFO_V1(jsonb_slice_path_text);
PG_FUNCTION_INFO_V1(jsonb_slice_test_fetched_bytes);
PG_FUNCTION_INFO_V1(jsonb_slice_test_function_state);
PG_FUNCTION_INFO_V1(jsonb_slice_test_statement_eligible);
PG_FUNCTION_INFO_V1(jsonb_slice_test_plan_stats);
PG_FUNCTION_INFO_V1(jsonb_slice_test_plan_stats_reset);

/* Last successful sliced call in this backend; exposed only to the tests. */
static int64 last_fetched_bytes = -1;

/*
 * Expose only stable validation counts to regression tests.  The planner
 * module owns the catalog cache; this SQL entry point only formats its three
 * answers and never exposes database-specific function OIDs.
 */
Datum
jsonb_slice_test_function_state(PG_FUNCTION_ARGS)
{
	char *state;
	text *result;
	int validated_count;
	int replacement_public_count;
	int builtin_public_count;

	jsonb_slice_plan_test_function_state(
		&validated_count, &replacement_public_count, &builtin_public_count);
	state = psprintf("%d:%d:%d", validated_count, replacement_public_count,
					 builtin_public_count);
	result = cstring_to_text(state);
	pfree(state);
	PG_RETURN_TEXT_P(result);
}

/* Expose the current statement's relation-gate result to regression tests. */
Datum
jsonb_slice_test_statement_eligible(PG_FUNCTION_ARGS)
{
	PG_RETURN_BOOL(jsonb_slice_plan_test_statement_eligible());
}

/* Return stable planner-rewrite counts without exposing plan-node details. */
Datum
jsonb_slice_test_plan_stats(PG_FUNCTION_ARGS)
{
	char *state;
	text *result;
	int64 plan_count;
	int64 call_count;

	jsonb_slice_plan_test_stats(&plan_count, &call_count);
	state = psprintf(INT64_FORMAT ":" INT64_FORMAT, plan_count, call_count);
	result = cstring_to_text(state);
	pfree(state);
	PG_RETURN_TEXT_P(result);
}

Datum
jsonb_slice_test_plan_stats_reset(PG_FUNCTION_ARGS)
{
	jsonb_slice_plan_test_stats_reset();
	PG_RETURN_BOOL(true);
}

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

/* Deconstruct a text path with the API available in this PostgreSQL version. */
static void
deconstruct_path_array(ArrayType *path_array, Datum **elements, bool **nulls,
					   int *element_count)
{
#if PG_VERSION_NUM >= 160000
	deconstruct_array_builtin(path_array, TEXTOID, elements, nulls,
							  element_count);
#else
	deconstruct_array(path_array, TEXTOID, -1, false, TYPALIGN_INT, elements,
					  nulls, element_count);
#endif
}

/* Parse one array step without raising an error for non-integer path text. */
static bool
path_array_index(text *element, int32 *index)
{
	char *input = text_to_cstring(element);
	char *end;
	long parsed;
	bool valid;

	errno = 0;
	parsed = strtol(input, &end, 10);
	valid = end != input && *end == '\0' && errno == 0 && parsed >= INT_MIN &&
			parsed <= INT_MAX;
	if (valid)
		*index = (int32)parsed;
	pfree(input);
	return valid;
}

/*
 * Follow a non-empty path through nested object and array containers.
 * Return false only when the caller must use PostgreSQL's complete-value
 * path. Return true with found=false when the path has no result.
 */
static bool
slice_path_value(FunctionCallInfo fcinfo, Datum *path_elements,
				 int path_element_count, SliceSource *source,
				 ValueLocation *value, bool *found)
{
	struct varlena *attr = (struct varlena *)PG_GETARG_POINTER(0);
	ContainerLocation container;
	ValueLocation located;
	int step;

	*found = false;
	if (!slice_source_init(attr, source))
		return false;

	container.start = 0;
	container.size = source->total_size;
	container.root = true;

	for (step = 0; step < path_element_count; step++)
	{
		text *element = DatumGetTextPP(path_elements[step]);
		uint32 item_count;
		uint32 header;
		bool step_found;
		bool valid;

		if (!slice_read_container_header(source, &container, &header))
			return false;
		item_count = header & JB_CMASK;

		if (header & JB_FOBJECT)
		{
			valid = slice_object_lookup(
				source, &container, item_count, VARDATA_ANY(element),
				VARSIZE_ANY_EXHDR(element), &located, &step_found);
		}
		else
		{
			int32 requested;
			uint32 index;

			if (!path_array_index(element, &requested) ||
				!slice_normalize_array_index(requested, item_count, &index))
				return true;
			valid = slice_array_lookup(source, &container, item_count, index,
									   &located, &step_found);
		}

		if (!valid)
			return false;
		if (!step_found)
			return true;
		if (step == path_element_count - 1)
		{
			*value = located;
			*found = true;
			return true;
		}

		/* More path remains, so the selected value must be a container. */
		if (located.type != JENTRY_ISCONTAINER)
			return true;
		container.start = located.start;
		container.size = located.size;
		container.root = false;
	}

	return true;
}

/*
 * Read and validate the SQL path argument, then locate its final value.
 * has_null_element distinguishes a NULL path element from a missing value;
 * both SQL functions return NULL without trying a storage fallback.
 */
static bool
slice_path_argument(FunctionCallInfo fcinfo, SliceSource *source,
					ValueLocation *value, bool *found, bool *has_null_element)
{
	ArrayType *path_array = PG_GETARG_ARRAYTYPE_P(1);
	Datum *path_elements;
	bool *path_nulls;
	int path_element_count;
	bool valid = false;
	int step;

	*found = false;
	*has_null_element = false;
	deconstruct_path_array(path_array, &path_elements, &path_nulls,
						   &path_element_count);

	/* An empty path returns the complete input, so slicing cannot reduce it. */
	if (path_element_count == 0)
		goto done;
	for (step = 0; step < path_element_count; step++)
	{
		if (path_nulls[step])
		{
			*has_null_element = true;
			valid = true;
			goto done;
		}
	}

	valid = slice_path_value(fcinfo, path_elements, path_element_count, source,
							 value, found);

done:
	pfree(path_elements);
	pfree(path_nulls);
	PG_FREE_IF_COPY(path_array, 1);
	return valid;
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

/* Return one nested value as JSONB, like the #> text[] operator. */
Datum
jsonb_slice_path(PG_FUNCTION_ARGS)
{
	SliceSource source;
	ValueLocation value;
	Datum result = (Datum)0;
	bool found;
	bool has_null_element;

	last_fetched_bytes = -1;
	if (!slice_path_argument(fcinfo, &source, &value, &found,
							 &has_null_element))
		return jsonb_extract_path(fcinfo);
	if (has_null_element)
		PG_RETURN_NULL();
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}
	if (!slice_value_result(&source, &value, &result))
		return jsonb_extract_path(fcinfo);

	last_fetched_bytes = source.fetched_bytes;
	return result;
}

/* Return one nested value as SQL text, like the #>> text[] operator. */
Datum
jsonb_slice_path_text(PG_FUNCTION_ARGS)
{
	SliceSource source;
	ValueLocation value;
	Datum result = (Datum)0;
	bool found;
	bool has_null_element;
	bool is_null;

	last_fetched_bytes = -1;
	if (!slice_path_argument(fcinfo, &source, &value, &found,
							 &has_null_element))
		return jsonb_extract_path_text(fcinfo);
	if (has_null_element)
		PG_RETURN_NULL();
	if (!found)
	{
		last_fetched_bytes = source.fetched_bytes;
		PG_RETURN_NULL();
	}
	if (!slice_value_text_result(&source, &value, &result, &is_null))
		return jsonb_extract_path_text(fcinfo);

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
