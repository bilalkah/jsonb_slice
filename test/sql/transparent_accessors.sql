-- Prove that each built-in JSONB accessor can use its matching jsonb_slice
-- function in a plan target list.
LOAD 'jsonb_slice';
CREATE EXTENSION jsonb_slice;

CREATE FUNCTION jsonb_slice_test_plan_stats()
RETURNS text
AS '$libdir/jsonb_slice', 'jsonb_slice_test_plan_stats'
LANGUAGE C;
CREATE FUNCTION jsonb_slice_test_plan_stats_reset()
RETURNS boolean
AS '$libdir/jsonb_slice', 'jsonb_slice_test_plan_stats_reset'
LANGUAGE C;

SET max_parallel_workers_per_gather = 0;

CREATE TABLE transparent_accessor_document
(
	id integer,
	object_value jsonb,
	array_value jsonb
);
ALTER TABLE transparent_accessor_document
	ALTER COLUMN object_value SET STORAGE EXTERNAL;
ALTER TABLE transparent_accessor_document
	ALTER COLUMN array_value SET STORAGE EXTERNAL;
INSERT INTO transparent_accessor_document VALUES
(
	1,
	jsonb_build_object(
		'name', 'Ada',
		'profile', jsonb_build_object('role', 'engineer'),
		'padding', repeat('x', 24000)
	),
	jsonb_build_array('first', 'middle', 'last', repeat('y', 24000))
);

-- With transparent mode off, all six built-in operators keep their original
-- executable functions.
SET jsonb_slice.transparent = off;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT
		(object_value -> 'name') IS NOT DISTINCT FROM
			jsonb_slice_get(object_value, 'name') AND
		(array_value -> -1) IS NOT DISTINCT FROM
			jsonb_slice_get(array_value, -1) AND
		(object_value ->> 'name') IS NOT DISTINCT FROM
			jsonb_slice_get_text(object_value, 'name') AND
		(array_value ->> 1) IS NOT DISTINCT FROM
			jsonb_slice_get_text(array_value, 1) AND
		(object_value #> '{profile,role}') IS NOT DISTINCT FROM
			jsonb_slice_path(object_value, ARRAY['profile', 'role']) AND
		(object_value #>> '{profile,role}') IS NOT DISTINCT FROM
			jsonb_slice_path_text(object_value, ARRAY['profile', 'role'])
	FROM transparent_accessor_document
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- With transparent mode on, the same operator expressions keep their answers
-- while all six executable function OIDs are replaced.
SET jsonb_slice.transparent = on;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT
		(object_value -> 'name') IS NOT DISTINCT FROM
			jsonb_slice_get(object_value, 'name') AND
		(array_value -> -1) IS NOT DISTINCT FROM
			jsonb_slice_get(array_value, -1) AND
		(object_value ->> 'name') IS NOT DISTINCT FROM
			jsonb_slice_get_text(object_value, 'name') AND
		(array_value ->> 1) IS NOT DISTINCT FROM
			jsonb_slice_get_text(array_value, 1) AND
		(object_value #> '{profile,role}') IS NOT DISTINCT FROM
			jsonb_slice_path(object_value, ARRAY['profile', 'role']) AND
		(object_value #>> '{profile,role}') IS NOT DISTINCT FROM
			jsonb_slice_path_text(object_value, ARRAY['profile', 'role'])
	FROM transparent_accessor_document
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- Direct calls of the six built-in functions use the same replacement table.
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT
		jsonb_object_field(object_value, 'name') IS NOT DISTINCT FROM
			jsonb_slice_get(object_value, 'name') AND
		jsonb_array_element(array_value, -1) IS NOT DISTINCT FROM
			jsonb_slice_get(array_value, -1) AND
		jsonb_object_field_text(object_value, 'name') IS NOT DISTINCT FROM
			jsonb_slice_get_text(object_value, 'name') AND
		jsonb_array_element_text(array_value, 1) IS NOT DISTINCT FROM
			jsonb_slice_get_text(array_value, 1) AND
		jsonb_extract_path(
			object_value, VARIADIC ARRAY['profile', 'role']) IS NOT DISTINCT FROM
			jsonb_slice_path(object_value, ARRAY['profile', 'role']) AND
		jsonb_extract_path_text(
			object_value, VARIADIC ARRAY['profile', 'role']) IS NOT DISTINCT FROM
			jsonb_slice_path_text(object_value, ARRAY['profile', 'role'])
	FROM transparent_accessor_document
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

RESET jsonb_slice.transparent;
DROP TABLE transparent_accessor_document;
DROP FUNCTION jsonb_slice_test_plan_stats_reset();
DROP FUNCTION jsonb_slice_test_plan_stats();
DROP EXTENSION jsonb_slice;
