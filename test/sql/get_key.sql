CREATE EXTENSION jsonb_slice;

CREATE FUNCTION jsonb_slice_test_fetched_bytes()
RETURNS bigint
AS '$libdir/jsonb_slice', 'jsonb_slice_test_fetched_bytes'
LANGUAGE C;

COPY (
    SELECT test_name || ':' ||
           (jsonb_slice_get(document, field_name) IS NOT DISTINCT FROM
            document -> field_name)
    FROM (VALUES
        ('string',       '{"field": "value"}'::jsonb,   'field'),
        ('number',       '{"field": 42}'::jsonb,        'field'),
        ('boolean',      '{"field": true}'::jsonb,      'field'),
        ('json null',    '{"field": null}'::jsonb,      'field'),
        ('object',       '{"field": {"n": 1}}'::jsonb, 'field'),
        ('array',        '{"field": [1, 2]}'::jsonb,    'field'),
        ('empty key',    '{"": "value"}'::jsonb,      ''),
        ('missing key',  '{"field": 1}'::jsonb,         'missing'),
        ('multibyte key', '{"ключ": "значение"}'::jsonb, 'ключ'),
        ('array input',  '[1, 2]'::jsonb,                 'field'),
        ('scalar input', '1'::jsonb,                      'field')
    ) AS cases(test_name, document, field_name)
    ORDER BY test_name
) TO STDOUT;

COPY (
    SELECT jsonb_slice_get(NULL::jsonb, 'field') IS NULL AND
           jsonb_slice_get('{"field": 1}'::jsonb, NULL::text) IS NULL
) TO STDOUT;

CREATE TABLE toasted_input (document jsonb);
ALTER TABLE toasted_input ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO toasted_input
SELECT jsonb_build_object(
	'', 'empty key',
	'array', jsonb_build_array(1, 2),
	'boolean', true,
	'boolean false', false,
	'decimal', 12345.6789,
	'empty string', '',
	'json null', NULL,
	'large number', 123456789012345678901234567890.123456789::numeric,
	'multibyte', 'çğıöşü 日本語',
	'negative', -42,
	'number', 42,
	'object', jsonb_build_object('value', 1),
	'string', 'value',
	'ключ', 'значение',
	'padding a', repeat('x', 12000),
	'padding b', repeat('y', 12000)
);

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get(document, field_name) IS NOT DISTINCT FROM
			document -> field_name)
	FROM toasted_input
	CROSS JOIN (VALUES
		('array', 'array'),
		('boolean', 'boolean'),
		('boolean false', 'boolean false'),
		('decimal', 'decimal'),
		('empty key', ''),
		('empty string', 'empty string'),
		('json null', 'json null'),
		('large number', 'large number'),
		('missing key', 'missing'),
		('multibyte key', 'ключ'),
		('multibyte value', 'multibyte'),
		('negative', 'negative'),
		('number', 'number'),
		('object', 'object'),
		('string', 'string')
	) AS cases(test_name, field_name)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() <
		   (SELECT pg_column_size(document) FROM toasted_input)
) TO STDOUT;

DROP TABLE toasted_input;

CREATE TABLE search_input (document jsonb);
ALTER TABLE search_input ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO search_input
SELECT jsonb_build_object(
	'a', 'first',
	'z', 'short key',
	'aa', 'longer key',
	'mm', 'middle left',
	'zz', 'middle',
	'large a', repeat('x', 12000),
	'large b', repeat('y', 12000),
	'zzzzzzzzzz', 'last'
);

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get(document, field_name) IS NOT DISTINCT FROM
			document -> field_name)
	FROM search_input
	CROSS JOIN (VALUES
		('first key', 'a'),
		('last key', 'zzzzzzzzzz'),
		('length ordering', 'z'),
		('longer key', 'aa'),
		('middle key', 'zz'),
		('missing key', 'missing')
	) AS cases(test_name, field_name)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get(document, 'a') = document -> 'a'
	FROM search_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() < pg_column_size(document)
	FROM search_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_get(document, 'zzzzzzzzzz') =
		   document -> 'zzzzzzzzzz'
	FROM search_input
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() < pg_column_size(document)
	FROM search_input
) TO STDOUT;

DROP TABLE search_input;

CREATE TABLE stride_input (document jsonb);
ALTER TABLE stride_input ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO stride_input
SELECT jsonb_object_agg('k' || lpad(number::text, 2, '0'), number) ||
	   jsonb_build_object('padding', repeat('p', 24000))
FROM generate_series(0, 39) AS numbers(number);

COPY (
	SELECT test_name || ':' ||
		   (jsonb_slice_get(document, field_name) IS NOT DISTINCT FROM
			document -> field_name)
	FROM stride_input
	CROSS JOIN (VALUES
		('after offset stride', 'k32'),
		('before offset stride', 'k31'),
		('first key', 'k00'),
		('last key', 'k39')
	) AS cases(test_name, field_name)
	ORDER BY test_name
) TO STDOUT;

COPY (
	SELECT jsonb_slice_test_fetched_bytes() > 0 AND
		   jsonb_slice_test_fetched_bytes() < pg_column_size(document)
	FROM stride_input
) TO STDOUT;

DROP TABLE stride_input;

DROP FUNCTION jsonb_slice_test_fetched_bytes();

DROP EXTENSION jsonb_slice;
