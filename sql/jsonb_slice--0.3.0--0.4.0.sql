\echo Use "ALTER EXTENSION jsonb_slice UPDATE TO '0.4.0'" to load this file. \quit

CREATE FUNCTION jsonb_slice_get(from_json jsonb, element_index integer)
RETURNS jsonb
AS 'MODULE_PATHNAME', 'jsonb_slice_get_index'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION jsonb_slice_get_text(from_json jsonb, field_name text)
RETURNS text
AS 'MODULE_PATHNAME', 'jsonb_slice_get_text_key'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION jsonb_slice_get_text(from_json jsonb, element_index integer)
RETURNS text
AS 'MODULE_PATHNAME', 'jsonb_slice_get_text_index'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION jsonb_slice_path(from_json jsonb, path_elements text[])
RETURNS jsonb
AS 'MODULE_PATHNAME', 'jsonb_slice_path'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

CREATE FUNCTION jsonb_slice_path_text(from_json jsonb, path_elements text[])
RETURNS text
AS 'MODULE_PATHNAME', 'jsonb_slice_path_text'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

COMMENT ON FUNCTION jsonb_slice_get(jsonb, integer) IS
'read one top-level array element with TOAST slices when the stored jsonb value permits it';

COMMENT ON FUNCTION jsonb_slice_get_text(jsonb, text) IS
'read one top-level field as text with TOAST slices when the stored jsonb value permits it';

COMMENT ON FUNCTION jsonb_slice_get_text(jsonb, integer) IS
'read one top-level array element as text with TOAST slices when the stored jsonb value permits it';

COMMENT ON FUNCTION jsonb_slice_path(jsonb, text[]) IS
'read one nested path with TOAST slices when the stored jsonb value permits it';

COMMENT ON FUNCTION jsonb_slice_path_text(jsonb, text[]) IS
'read one nested path as text with TOAST slices when the stored jsonb value permits it';

COMMENT ON EXTENSION jsonb_slice IS
'Study partial reads of large jsonb values; 0.4.0 adds nested path accessors';
