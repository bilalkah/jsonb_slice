-- This is the first transparent vertical slice.  Only PostgreSQL's
-- jsonb_object_field_text(jsonb, text), used by ->> text, may be replaced.
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

CREATE TABLE transparent_text_document (id integer, document jsonb);
ALTER TABLE transparent_text_document
	ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO transparent_text_document VALUES
	(1, jsonb_build_object('name', 'first', 'padding', repeat('x', 24000))),
	(2, jsonb_build_object('name', 'second', 'padding', repeat('y', 24000)));

-- With transparent mode off, the built-in answer is unchanged and no plan
-- call is replaced.  Stats are plans:calls.
SET jsonb_slice.transparent = off;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY (
	SELECT id || ':' ||
		   ((document ->> 'name') IS NOT DISTINCT FROM
			jsonb_slice_get_text(document, 'name'))
	FROM transparent_text_document
	ORDER BY id
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- The operator in one target-list expression now calls the validated
-- replacement while returning the same text.
SET jsonb_slice.transparent = on;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY (
	SELECT id || ':' ||
		   ((document ->> 'name') IS NOT DISTINCT FROM
			jsonb_slice_get_text(document, 'name'))
	FROM transparent_text_document
	ORDER BY id
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- A direct call of the built-in function follows the same path.
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY (
	SELECT id || ':' ||
		   (jsonb_object_field_text(document, 'name') IS NOT DISTINCT FROM
			jsonb_slice_get_text(document, 'name'))
	FROM transparent_text_document
	ORDER BY id
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- The common Plan filter field is also rewritten.
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY (
	SELECT id
	FROM transparent_text_document
	WHERE document ->> 'name' = 'second'
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- OpExpr keeps its operator OID, so EXPLAIN still prints ->>.
\pset format unaligned
EXPLAIN (VERBOSE, COSTS OFF)
SELECT document ->> 'name'
FROM transparent_text_document;

-- Without PUBLIC EXECUTE on the replacement, the built-in stays in the plan.
REVOKE EXECUTE ON FUNCTION jsonb_slice_get_text(jsonb, text) FROM PUBLIC;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY (
	SELECT id || ':' || (document ->> 'name')
	FROM transparent_text_document
	WHERE id = 1
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;
GRANT EXECUTE ON FUNCTION jsonb_slice_get_text(jsonb, text) TO PUBLIC;

-- A setting change discards a forced generic plan.  A second execution then
-- reuses the rewritten generic plan without another planner rewrite.
SET plan_cache_mode = force_generic_plan;
SET jsonb_slice.transparent = off;
PREPARE transparent_text_query(text) AS
	SELECT document ->> $1 FROM transparent_text_document WHERE id = 1;

COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
EXECUTE transparent_text_query('name');
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

SET jsonb_slice.transparent = on;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
EXECUTE transparent_text_query('name');
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
EXECUTE transparent_text_query('name');
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- The plan depends on both function grants.  REVOKE replans with the built-in;
-- GRANT invalidates that decision and permits replacement again.
REVOKE EXECUTE ON FUNCTION jsonb_slice_get_text(jsonb, text) FROM PUBLIC;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
EXECUTE transparent_text_query('name');
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

GRANT EXECUTE ON FUNCTION jsonb_slice_get_text(jsonb, text) TO PUBLIC;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
EXECUTE transparent_text_query('name');
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

\pset format aligned
DEALLOCATE transparent_text_query;
RESET plan_cache_mode;
RESET jsonb_slice.transparent;
DROP TABLE transparent_text_document;
DROP FUNCTION jsonb_slice_test_plan_stats_reset();
DROP FUNCTION jsonb_slice_test_plan_stats();
DROP EXTENSION jsonb_slice;
