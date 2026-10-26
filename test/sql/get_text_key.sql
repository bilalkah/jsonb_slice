CREATE EXTENSION jsonb_slice;

CREATE FUNCTION jsonb_slice_test_fetched_bytes()
RETURNS bigint
AS '$libdir/jsonb_slice', 'jsonb_slice_test_fetched_bytes'
LANGUAGE C;

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get_text(document, field_name) IS NOT DISTINCT FROM
			document ->> field_name)
	FROM (VALUES
		('array', '{"field": [1, 2]}'::jsonb, 'field'),
		('boolean', '{"field": true}'::jsonb, 'field'),
		('json null', '{"field": null}'::jsonb, 'field'),
		('missing key', '{"field": 1}'::jsonb, 'missing'),
		('number', '{"field": 42}'::jsonb, 'field'),
		('object', '{"field": {"n": 1}}'::jsonb, 'field'),
		('scalar input', '1'::jsonb, 'field'),
		('string', '{"field": "value"}'::jsonb, 'field')
	) AS cases(test_name, document, field_name)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get_text(NULL::jsonb, 'field') IS NULL AND
		   jsonb_slice_get_text('{"field": 1}'::jsonb, NULL::text) IS NULL
) TO STDOUT;

CREATE TABLE text_key_input (document jsonb);
ALTER TABLE text_key_input ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO text_key_input
SELECT jsonb_build_object(
	'array', jsonb_build_array(1, 'two'),
	'boolean', true,
	'boolean false', false,
	'decimal', 12345.6789,
	'empty string', '',
	'json null', NULL,
	'multibyte', 'çğıöşü 日本語',
	'number', 42,
	'object', jsonb_build_object('name', 'nested'),
	'string', 'value',
	'padding a', repeat('x', 12000),
	'padding b', repeat('y', 12000)
);

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get_text(document, field_name) IS NOT DISTINCT FROM
			document ->> field_name)
	FROM text_key_input
	CROSS JOIN (VALUES
		('array', 'array'),
		('boolean', 'boolean'),
		('boolean false', 'boolean false'),
		('decimal', 'decimal'),
		('empty string', 'empty string'),
		('json null', 'json null'),
		('missing key', 'missing'),
		('multibyte', 'multibyte'),
		('number', 'number'),
		('object', 'object'),
		('string', 'string')
	) AS cases(test_name, field_name)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get_text(document, 'string') = document ->> 'string'
	FROM text_key_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() < pg_column_size(document)
	FROM text_key_input
) TO STDOUT;

DROP TABLE text_key_input;

CREATE TABLE compressed_text_input (document jsonb);
INSERT INTO compressed_text_input
SELECT jsonb_build_object('field', 'value', 'padding', repeat('z', 24000));

COPY (
	SELECT pg_column_size(document) < 2000 AND
		   jsonb_slice_get_text(document, 'field') = document ->> 'field'
	FROM compressed_text_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() IS NULL
) TO STDOUT;

DROP TABLE compressed_text_input;
DROP FUNCTION jsonb_slice_test_fetched_bytes();
DROP EXTENSION jsonb_slice;
