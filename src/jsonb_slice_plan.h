/*
 * jsonb_slice_plan.h
 *
 * Planner-module initialization for jsonb_slice.
 */
#ifndef JSONB_SLICE_PLAN_H
#define JSONB_SLICE_PLAN_H

extern void jsonb_slice_plan_init(void);

/* Test-only summary; no SQL object installs this function. */
extern void jsonb_slice_plan_test_function_state(int *validated_count,
												 int *replacement_public_count,
												 int *builtin_public_count);
extern bool jsonb_slice_plan_test_statement_eligible(void);
extern void jsonb_slice_plan_test_stats(int64 *plan_count, int64 *call_count);
extern void jsonb_slice_plan_test_stats_reset(void);

#endif /* JSONB_SLICE_PLAN_H */
