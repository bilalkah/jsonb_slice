CREATE EXTENSION jsonb_slice;

CREATE FUNCTION jsonb_slice_test_fetched_bytes()
RETURNS bigint
AS '$libdir/jsonb_slice', 'jsonb_slice_test_fetched_bytes'
LANGUAGE C;

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get(document, element_index)
			IS NOT DISTINCT FROM document -> element_index)
	FROM (VALUES
		('array first', '[10, 20, 30]'::jsonb, 0),
		('array last negative', '[10, 20, 30]'::jsonb, -1),
		('array past end', '[10, 20, 30]'::jsonb, 3),
		('object input', '{"0": "zero"}'::jsonb, 0),
		('scalar minus one', '42'::jsonb, -1),
		('scalar past end', '42'::jsonb, 1),
		('scalar zero', '42'::jsonb, 0)
	) AS cases(test_name, document, element_index)
	ORDER BY test_name
) TO STDOUT;

CREATE TABLE array_input (document jsonb);
ALTER TABLE array_input ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO array_input
SELECT jsonb_build_array(
	'first',
	jsonb_build_object('name', 'nested'),
	true,
	NULL,
	42,
	repeat('x', 24000)
);

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get(document, element_index)
			IS NOT DISTINCT FROM document -> element_index)
	FROM array_input
	CROSS JOIN (VALUES
		('boolean', 2),
		('first', 0),
		('json null', 3),
		('last negative', -1),
		('negative first', -6),
		('number', 4),
		('object', 1),
		('past end', 6),
		('past start', -7)
	) AS cases(test_name, element_index)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get(document, 0) = document -> 0
	FROM array_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() < pg_column_size(document)
	FROM array_input
) TO STDOUT;

DROP TABLE array_input;
DROP FUNCTION jsonb_slice_test_fetched_bytes();
DROP EXTENSION jsonb_slice;
