/*
 * jsonb_slice.c
 *
 * Version 0.2.0 provides one top-level object accessor.  It reads an
 * in-memory JsonbContainer and returns the same jsonb value as -> with a text
 * key.  PG_GETARG_JSONB_P detoasts the complete input before the lookup, so
 * this version does not perform a partial TOAST read.
 */
#include "postgres.h"

#include "fmgr.h"
#include "utils/jsonb.h"

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_slice_get);

Datum
jsonb_slice_get(PG_FUNCTION_ARGS)
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
