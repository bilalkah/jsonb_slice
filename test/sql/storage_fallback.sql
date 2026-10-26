CREATE EXTENSION jsonb_slice;

CREATE FUNCTION jsonb_slice_test_fetched_bytes()
RETURNS bigint
AS '$libdir/jsonb_slice', 'jsonb_slice_test_fetched_bytes'
LANGUAGE C;

CREATE TABLE external_non_object_input (test_name text, document jsonb);
ALTER TABLE external_non_object_input
	ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO external_non_object_input VALUES
	('array', jsonb_build_array(repeat('a', 24000))),
	('scalar', to_jsonb(repeat('s', 24000)::text));

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get(document, 'field') IS NOT DISTINCT FROM
			document -> 'field')
	FROM external_non_object_input
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get(document, 'field') IS NULL
	FROM external_non_object_input
	WHERE test_name = 'scalar'
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() = 4
) TO STDOUT;

DROP TABLE external_non_object_input;

COPY (
	SELECT jsonb_slice_get('{"field": 1}'::jsonb, 'field') = '1'::jsonb
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() IS NULL
) TO STDOUT;

CREATE TABLE compressed_input (document jsonb);
INSERT INTO compressed_input
SELECT jsonb_build_object('field', 'value', 'padding', repeat('z', 24000));

COPY (
	SELECT pg_column_size(document) < 2000 AND
		   jsonb_slice_get(document, 'field') = document -> 'field'
	FROM compressed_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() IS NULL
) TO STDOUT;

DROP TABLE compressed_input;
DROP FUNCTION jsonb_slice_test_fetched_bytes();
DROP EXTENSION jsonb_slice;
