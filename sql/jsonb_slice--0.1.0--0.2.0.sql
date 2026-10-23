\echo Use "ALTER EXTENSION jsonb_slice UPDATE TO '0.2.0'" to load this file. \quit

CREATE FUNCTION jsonb_slice_get(from_json jsonb, field_name text)
RETURNS jsonb
AS 'MODULE_PATHNAME', 'jsonb_slice_get'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION jsonb_slice_get(jsonb, text) IS
'read one top-level object field after PostgreSQL detoasts the complete jsonb value';

COMMENT ON EXTENSION jsonb_slice IS
'Study partial reads of large jsonb values; 0.2.0 provides one in-memory accessor';
