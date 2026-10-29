/*
 * jsonb_slice_plan_internal.h
 *
 * Private contracts shared by the planner modules.  Nothing in this header
 * is installed as a SQL or public extension API.
 */
#ifndef JSONB_SLICE_PLAN_INTERNAL_H
#define JSONB_SLICE_PLAN_INTERNAL_H

#include "nodes/plannodes.h"

#define JSONB_SLICE_ACCESSOR_COUNT 6

/* Evidence collected while one PlannedStmt is inspected. */
typedef struct RewriteState
{
	bool replacement_dependency[JSONB_SLICE_ACCESSOR_COUNT];
	bool builtin_dependency[JSONB_SLICE_ACCESSOR_COUNT];
	int64 calls_replaced;
} RewriteState;

/* Function discovery, validation, permission decisions, and dependencies. */
extern void jsonb_slice_plan_functions_init(void);
extern void jsonb_slice_plan_ensure_function_state(void);
extern bool jsonb_slice_plan_replacement(Node *node, RewriteState *state,
										 Oid *replacement_oid);
extern bool
jsonb_slice_plan_rewrite_consulted_function(const RewriteState *state);
extern void
jsonb_slice_plan_add_function_dependencies(PlannedStmt *statement,
										   const RewriteState *state);

/* Relation eligibility and its relcache-backed cache. */
extern void jsonb_slice_plan_relations_init(void);
extern bool jsonb_slice_plan_statement_is_eligible(List *range_table);

/* Copy-on-write expression and plan traversal. */
extern Plan *jsonb_slice_plan_rewrite_plan(Plan *plan, RewriteState *state);
extern List *jsonb_slice_plan_rewrite_plan_list(List *plans,
												RewriteState *state);

#endif /* JSONB_SLICE_PLAN_INTERNAL_H */
