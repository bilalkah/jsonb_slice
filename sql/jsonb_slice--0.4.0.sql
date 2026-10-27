\echo Use "CREATE EXTENSION jsonb_slice" to load this file. \quit

CREATE FUNCTION jsonb_slice_get(from_json jsonb, field_name text)
RETURNS jsonb
AS 'MODULE_PATHNAME', 'jsonb_slice_get'
LANGUAGE C IMMUTABLE STRICT PARALLEL SAFE;

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

CREATE OPERATOR ~> (
	LEFTARG = jsonb,
	RIGHTARG = text,
	PROCEDURE = jsonb_slice_get
);

CREATE OPERATOR ~> (
	LEFTARG = jsonb,
	RIGHTARG = integer,
	PROCEDURE = jsonb_slice_get
);

CREATE OPERATOR ~>> (
	LEFTARG = jsonb,
	RIGHTARG = text,
	PROCEDURE = jsonb_slice_get_text
);

CREATE OPERATOR ~>> (
	LEFTARG = jsonb,
	RIGHTARG = integer,
	PROCEDURE = jsonb_slice_get_text
);

CREATE OPERATOR ~#> (
	LEFTARG = jsonb,
	RIGHTARG = text[],
	PROCEDURE = jsonb_slice_path
);

CREATE OPERATOR ~#>> (
	LEFTARG = jsonb,
	RIGHTARG = text[],
	PROCEDURE = jsonb_slice_path_text
);

COMMENT ON FUNCTION jsonb_slice_get(jsonb, text) IS
'read one top-level field with TOAST slices when the stored jsonb value permits it';

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

COMMENT ON OPERATOR ~> (jsonb, text) IS
'read one top-level object field as JSONB with jsonb_slice_get';

COMMENT ON OPERATOR ~> (jsonb, integer) IS
'read one top-level array element as JSONB with jsonb_slice_get';

COMMENT ON OPERATOR ~>> (jsonb, text) IS
'read one top-level object field as text with jsonb_slice_get_text';

COMMENT ON OPERATOR ~>> (jsonb, integer) IS
'read one top-level array element as text with jsonb_slice_get_text';

COMMENT ON OPERATOR ~#> (jsonb, text[]) IS
'read one nested path as JSONB with jsonb_slice_path';

COMMENT ON OPERATOR ~#>> (jsonb, text[]) IS
'read one nested path as text with jsonb_slice_path_text';

COMMENT ON EXTENSION jsonb_slice IS
'Study partial reads of large jsonb values; 0.4.0 adds accessors and matching operators';
