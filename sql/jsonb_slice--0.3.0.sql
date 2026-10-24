\echo Use "CREATE EXTENSION jsonb_slice" to load this file. \quit

CREATE FUNCTION jsonb_slice_get(from_json jsonb, field_name text)
RETURNS jsonb
AS 'MODULE_PATHNAME', 'jsonb_slice_get'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION jsonb_slice_get(jsonb, text) IS
'read one top-level field with TOAST slices when the stored jsonb value permits it';

COMMENT ON EXTENSION jsonb_slice IS
'Study partial reads of large jsonb values; 0.3.0 adds a sliced read path';
