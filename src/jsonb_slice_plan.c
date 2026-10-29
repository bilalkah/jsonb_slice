/*
 * jsonb_slice_plan.c
 *
 * Planner-hook lifecycle and orchestration for jsonb_slice.  Function safety,
 * relation eligibility, and copy-on-write traversal live in focused modules;
 * this file connects their answers without owning their implementation.
 */
#include "postgres.h"

#include "commands/explain.h"
#include "optimizer/planner.h"
#include "utils/guc.h"
#include "utils/plancache.h"

#include "jsonb_slice_plan.h"
#include "jsonb_slice_plan_internal.h"

static bool transparent_mode = false;
static planner_hook_type previous_planner_hook = NULL;

/* Backend-local evidence used only by regression-test SQL functions. */
static int64 plans_rewritten = 0;
static int64 calls_replaced = 0;

/* Reset cached plans only when transparent mode actually changes. */
static void
assign_transparent(bool new_value, void *extra)
{
	if (new_value != transparent_mode)
		ResetPlanCache();
}

/*
 * Inspect one completed PlannedStmt.  The support modules first provide the
 * current function and relation answers.  The walker then returns original
 * plan pointers for unchanged trees and copied pointers for changed trees.
 */
static PlannedStmt *
rewrite_statement(PlannedStmt *statement)
{
	RewriteState state;
	Plan *plan_tree;
	List *subplans;
	PlannedStmt *result;

	jsonb_slice_plan_ensure_function_state();
	if (!jsonb_slice_plan_statement_is_eligible(statement->rtable) ||
		!transparent_mode)
		return statement;

	memset(&state, 0, sizeof(state));
	plan_tree = jsonb_slice_plan_rewrite_plan(statement->planTree, &state);
	subplans = jsonb_slice_plan_rewrite_plan_list(statement->subplans, &state);
	if (plan_tree == statement->planTree && subplans == statement->subplans &&
		!jsonb_slice_plan_rewrite_consulted_function(&state))
		return statement;

	result = (PlannedStmt *)memcpy(palloc(sizeof(PlannedStmt)), statement,
								   sizeof(PlannedStmt));
	result->planTree = plan_tree;
	result->subplans = subplans;
	jsonb_slice_plan_add_function_dependencies(result, &state);
	if (state.calls_replaced > 0)
	{
		plans_rewritten++;
		calls_replaced += state.calls_replaced;
	}
	return result;
}

/*
 * PostgreSQL calls this wrapper for each statement planned after the library
 * loads.  PostgreSQL 19 adds ExplainState to the hook signature.  Both forms
 * call the previous planner chain first and rewrite only its finished plan.
 */
#if PG_VERSION_NUM >= 190000
static PlannedStmt *
jsonb_slice_planner(Query *query, const char *query_string, int cursor_options,
					ParamListInfo bound_parameters,
					ExplainState *explain_state)
{
	PlannedStmt *statement;

	if (previous_planner_hook != NULL)
		statement = previous_planner_hook(query, query_string, cursor_options,
										  bound_parameters, explain_state);
	else
		statement = standard_planner(query, query_string, cursor_options,
									 bound_parameters, explain_state);

	return rewrite_statement(statement);
}
#else
static PlannedStmt *
jsonb_slice_planner(Query *query, const char *query_string, int cursor_options,
					ParamListInfo bound_parameters)
{
	PlannedStmt *statement;

	if (previous_planner_hook != NULL)
		statement = previous_planner_hook(query, query_string, cursor_options,
										  bound_parameters);
	else
		statement = standard_planner(query, query_string, cursor_options,
									 bound_parameters);

	return rewrite_statement(statement);
}
#endif

/* Install the setting, planner wrapper, and support-module callbacks. */
void
jsonb_slice_plan_init(void)
{
	DefineCustomBoolVariable(
		"jsonb_slice.transparent",
		"Lets built-in JSONB accessors use jsonb_slice after planning.",
		"The setting is off by default.", &transparent_mode, false,
		PGC_USERSET, 0, NULL, assign_transparent, NULL);
	previous_planner_hook = planner_hook;
	planner_hook = jsonb_slice_planner;
	jsonb_slice_plan_functions_init();
	jsonb_slice_plan_relations_init();

	MarkGUCPrefixReserved("jsonb_slice");
}

void
jsonb_slice_plan_test_stats(int64 *plan_count, int64 *call_count)
{
	*plan_count = plans_rewritten;
	*call_count = calls_replaced;
}

void
jsonb_slice_plan_test_stats_reset(void)
{
	plans_rewritten = 0;
	calls_replaced = 0;
}
