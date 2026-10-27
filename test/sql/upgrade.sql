CREATE EXTENSION jsonb_slice VERSION '0.1.0';

COPY (
    SELECT extversion || ':' ||
           (to_regprocedure('jsonb_slice_get(jsonb,text)') IS NULL)
    FROM pg_extension
    WHERE extname = 'jsonb_slice'
) TO STDOUT;

ALTER EXTENSION jsonb_slice UPDATE TO '0.2.0';

COPY (
    SELECT extversion || ':' ||
           (jsonb_slice_get('{"field": "value"}'::jsonb, 'field') = '"value"'::jsonb)
    FROM pg_extension
    WHERE extname = 'jsonb_slice'
) TO STDOUT;

ALTER EXTENSION jsonb_slice UPDATE TO '0.3.0';

COPY (
    SELECT extversion || ':' ||
           (jsonb_slice_get('{"field": "value"}'::jsonb, 'field') = '"value"'::jsonb)
    FROM pg_extension
    WHERE extname = 'jsonb_slice'
) TO STDOUT;

ALTER EXTENSION jsonb_slice UPDATE TO '0.4.0';

COPY (
    SELECT extversion || ':' ||
           (jsonb_slice_get('[10, 20, 30]'::jsonb, 1) = '20'::jsonb) || ':' ||
           (jsonb_slice_get_text('{"field": "value"}'::jsonb, 'field') = 'value') || ':' ||
           (jsonb_slice_get_text('[10, 20, 30]'::jsonb, 1) = '20') || ':' ||
           (jsonb_slice_path('{"a": {"b": [10, 20]}}'::jsonb,
                             ARRAY['a', 'b', '1']) = '20'::jsonb) || ':' ||
           (jsonb_slice_path_text('{"a": {"b": [10, 20]}}'::jsonb,
                                  ARRAY['a', 'b', '1']) = '20') || ':' ||
           ((('{"field": "value"}'::jsonb ~>> 'field') = 'value') AND
            (('[10, 20]'::jsonb ~> 1) = '20'::jsonb) AND
            (('{"a": {"b": 20}}'::jsonb ~#>> ARRAY['a', 'b']) = '20'))
    FROM pg_extension
    WHERE extname = 'jsonb_slice'
) TO STDOUT;

DROP EXTENSION jsonb_slice;
