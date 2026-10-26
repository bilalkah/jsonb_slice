CREATE EXTENSION jsonb_slice;

CREATE FUNCTION jsonb_slice_test_fetched_bytes()
RETURNS bigint
AS '$libdir/jsonb_slice', 'jsonb_slice_test_fetched_bytes'
LANGUAGE C;

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get_text(document, element_index)
			IS NOT DISTINCT FROM document ->> element_index)
	FROM (VALUES
		('array value', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 5),
		('boolean', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 2),
		('first', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 0),
		('json null', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 3),
		('last negative', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, -1),
		('number', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 1),
		('object input', '{"0": "zero"}'::jsonb, 0),
		('object value', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 4),
		('past end', '["first", 42, true, null, {"n": 1}, [1, 2]]'::jsonb, 6),
		('scalar minus one', '42'::jsonb, -1),
		('scalar past end', '42'::jsonb, 1),
		('scalar zero', '42'::jsonb, 0)
	) AS cases(test_name, document, element_index)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get_text(NULL::jsonb, 0) IS NULL AND
		   jsonb_slice_get_text('[1]'::jsonb, NULL::integer) IS NULL
) TO STDOUT;

CREATE TABLE text_index_input (document jsonb);
ALTER TABLE text_index_input ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO text_index_input
SELECT jsonb_build_array(
	'first',
	jsonb_build_object('name', 'nested'),
	true,
	false,
	NULL,
	42,
	jsonb_build_array(1, 'two'),
	repeat('x', 24000)
);

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get_text(document, element_index)
			IS NOT DISTINCT FROM document ->> element_index)
	FROM text_index_input
	CROSS JOIN (VALUES
		('array', 6),
		('boolean', 2),
		('boolean false', 3),
		('first', 0),
		('json null', 4),
		('last negative', -1),
		('negative first', -8),
		('number', 5),
		('object', 1),
		('past end', 8),
		('past start', -9)
	) AS cases(test_name, element_index)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get_text(document, 0) = document ->> 0
	FROM text_index_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() < pg_column_size(document)
	FROM text_index_input
) TO STDOUT;

DROP TABLE text_index_input;

CREATE TABLE compressed_text_index_input (document jsonb);
INSERT INTO compressed_text_index_input
SELECT jsonb_build_array('value', repeat('z', 24000));

COPY (
	SELECT pg_column_size(document) < 2000 AND
		   jsonb_slice_get_text(document, 0) = document ->> 0
	FROM compressed_text_index_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() IS NULL
) TO STDOUT;

DROP TABLE compressed_text_index_input;
DROP FUNCTION jsonb_slice_test_fetched_bytes();
DROP EXTENSION jsonb_slice;
