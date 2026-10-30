-- Exercise the common Plan fields and the child-plan containers handled by
-- the copy-on-write walker.
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
SET jsonb_slice.transparent = on;

CREATE TABLE walker_document (id integer, document jsonb);
ALTER TABLE walker_document ALTER COLUMN document SET STORAGE EXTERNAL;
INSERT INTO walker_document VALUES
	(1, jsonb_build_object('name', 'Ada', 'padding', repeat('x', 24000))),
	(2, jsonb_build_object('name', 'Grace', 'padding', repeat('y', 24000)));
ANALYZE walker_document;

\pset format unaligned

-- Agg is the root.  The walker must pass through it to reach the SeqScan
-- filter stored below it.
EXPLAIN (COSTS OFF)
SELECT count(*)
FROM walker_document
WHERE document ->> 'name' = 'Ada';
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT count(*)
	FROM walker_document
	WHERE document ->> 'name' = 'Ada'
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- Limit and Sort are both above the SeqScan that owns this filter.
EXPLAIN (COSTS OFF)
SELECT id
FROM walker_document
WHERE document ->> 'name' = 'Grace'
ORDER BY id
LIMIT 1;
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT id
	FROM walker_document
	WHERE document ->> 'name' = 'Grace'
	ORDER BY id
	LIMIT 1
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- Append stores its children in appendplans instead of lefttree or righttree.
EXPLAIN (COSTS OFF)
SELECT id
FROM walker_document
WHERE document ->> 'name' = 'Ada'
UNION ALL
SELECT id
FROM walker_document
WHERE document ->> 'name' = 'Grace';
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT id
	FROM walker_document
	WHERE document ->> 'name' = 'Ada'
	UNION ALL
	SELECT id
	FROM walker_document
	WHERE document ->> 'name' = 'Grace'
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

-- The scalar subquery has its own plan in PlannedStmt.subplans.
COPY (SELECT jsonb_slice_test_plan_stats_reset()) TO STDOUT;
COPY
(
	SELECT outer_document.id || ':' ||
		   (SELECT inner_document.document ->> 'name'
			FROM walker_document AS inner_document
			WHERE inner_document.id = outer_document.id)
	FROM walker_document AS outer_document
	WHERE outer_document.id = 1
) TO STDOUT;
COPY (SELECT jsonb_slice_test_plan_stats()) TO STDOUT;

\pset format aligned
RESET jsonb_slice.transparent;
DROP TABLE walker_document;
DROP FUNCTION jsonb_slice_test_plan_stats_reset();
DROP FUNCTION jsonb_slice_test_plan_stats();
DROP EXTENSION jsonb_slice;
