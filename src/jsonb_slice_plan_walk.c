/*
 * jsonb_slice_plan_walk.c
 *
 * Copy-on-write traversal of finished PostgreSQL plans.  This module changes
 * only executable accessor-function OIDs.  It does not change the parsed
 * Query, planner choices, operator identity, arguments, or result types.
 */
#include "postgres.h"

#include "miscadmin.h"
#include "nodes/nodeFuncs.h"
#include "nodes/plannodes.h"

#include "jsonb_slice_plan_internal.h"

/*
 * A read-only pass avoids copying an expression that contains no replaceable
 * accessor.  A rejected permission decision can still record dependencies.
 */
static bool
expression_has_target(Node *node, void *context)
{
	RewriteState *state = (RewriteState *)context;
	Oid replacement_oid;

	if (node == NULL)
		return false;
	if (jsonb_slice_plan_replacement(node, state, &replacement_oid))
		return true;
	return expression_tree_walker(node, expression_has_target, context);
}

/*
 * Copy and rewrite one expression tree.  OpExpr keeps opno and changes only
 * opfuncid, so EXPLAIN retains the original operator.  FuncExpr changes only
 * funcid.  A subtree with no changed call returns its original pointer.
 */
static Node *
rewrite_expression_node(Node *node, void *context)
{
	RewriteState *state = (RewriteState *)context;
	int64 calls_before;
	Oid replacement_oid;
	Node *result;

	if (node == NULL)
		return NULL;

	calls_before = state->calls_replaced;
	result = expression_tree_mutator(node, rewrite_expression_node, context);
	if (jsonb_slice_plan_replacement(node, state, &replacement_oid))
	{
		if (IsA(result, OpExpr))
			((OpExpr *)result)->opfuncid = replacement_oid;
		else
			((FuncExpr *)result)->funcid = replacement_oid;
		state->calls_replaced++;
	}

	if (state->calls_replaced == calls_before)
		return node;
	return result;
}

static Node *
rewrite_expression(Node *node, RewriteState *state)
{
	if (node == NULL || !expression_has_target(node, state))
		return node;
	return rewrite_expression_node(node, state);
}

/*
 * Return the concrete size of every standard PostgreSQL 18/19 plan node.
 * Every concrete structure begins with Plan, but sizeof(Plan) is not enough
 * to copy its fields.  An unknown tag returns zero and remains unchanged.
 */
static Size
plan_node_size(NodeTag tag)
{
	switch (tag)
	{
	case T_Result:
		return sizeof(Result);
	case T_ProjectSet:
		return sizeof(ProjectSet);
	case T_ModifyTable:
		return sizeof(ModifyTable);
	case T_Append:
		return sizeof(Append);
	case T_MergeAppend:
		return sizeof(MergeAppend);
	case T_RecursiveUnion:
		return sizeof(RecursiveUnion);
	case T_BitmapAnd:
		return sizeof(BitmapAnd);
	case T_BitmapOr:
		return sizeof(BitmapOr);
	case T_SeqScan:
		return sizeof(SeqScan);
	case T_SampleScan:
		return sizeof(SampleScan);
	case T_IndexScan:
		return sizeof(IndexScan);
	case T_IndexOnlyScan:
		return sizeof(IndexOnlyScan);
	case T_BitmapIndexScan:
		return sizeof(BitmapIndexScan);
	case T_BitmapHeapScan:
		return sizeof(BitmapHeapScan);
	case T_TidScan:
		return sizeof(TidScan);
	case T_TidRangeScan:
		return sizeof(TidRangeScan);
	case T_SubqueryScan:
		return sizeof(SubqueryScan);
	case T_FunctionScan:
		return sizeof(FunctionScan);
	case T_ValuesScan:
		return sizeof(ValuesScan);
	case T_TableFuncScan:
		return sizeof(TableFuncScan);
	case T_CteScan:
		return sizeof(CteScan);
	case T_NamedTuplestoreScan:
		return sizeof(NamedTuplestoreScan);
	case T_WorkTableScan:
		return sizeof(WorkTableScan);
	case T_ForeignScan:
		return sizeof(ForeignScan);
	case T_CustomScan:
		return sizeof(CustomScan);
	case T_NestLoop:
		return sizeof(NestLoop);
	case T_MergeJoin:
		return sizeof(MergeJoin);
	case T_HashJoin:
		return sizeof(HashJoin);
	case T_Material:
		return sizeof(Material);
	case T_Memoize:
		return sizeof(Memoize);
	case T_Sort:
		return sizeof(Sort);
	case T_IncrementalSort:
		return sizeof(IncrementalSort);
	case T_Group:
		return sizeof(Group);
	case T_Agg:
		return sizeof(Agg);
	case T_WindowAgg:
		return sizeof(WindowAgg);
	case T_Unique:
		return sizeof(Unique);
	case T_Gather:
		return sizeof(Gather);
	case T_GatherMerge:
		return sizeof(GatherMerge);
	case T_Hash:
		return sizeof(Hash);
	case T_SetOp:
		return sizeof(SetOp);
	case T_LockRows:
		return sizeof(LockRows);
	case T_Limit:
		return sizeof(Limit);
	default:
		return 0;
	}
}

/*
 * Rewrite a list whose elements are child Plan pointers.  A NULL subplan is
 * valid.  The list itself is copied only after one child changes.
 */
List *
jsonb_slice_plan_rewrite_plan_list(List *plans, RewriteState *state)
{
	List *copy = NULL;
	ListCell *cell;
	int plan_index = 0;

	foreach (cell, plans)
	{
		Plan *original = (Plan *)lfirst(cell);
		Plan *rewritten = jsonb_slice_plan_rewrite_plan(original, state);

		if (rewritten != original)
		{
			if (copy == NULL)
				copy = list_copy(plans);
			lfirst(list_nth_cell(copy, plan_index)) = rewritten;
		}
		plan_index++;
	}
	return copy != NULL ? copy : plans;
}

/* Return the extra child-plan list owned by selected concrete node types. */
static List *
additional_child_plans(Plan *plan)
{
	switch (nodeTag(plan))
	{
	case T_Append:
		return ((Append *)plan)->appendplans;
	case T_MergeAppend:
		return ((MergeAppend *)plan)->mergeplans;
	case T_BitmapAnd:
		return ((BitmapAnd *)plan)->bitmapplans;
	case T_BitmapOr:
		return ((BitmapOr *)plan)->bitmapplans;
	case T_CustomScan:
		return ((CustomScan *)plan)->custom_plans;
	default:
		return NULL;
	}
}

/* Install a rewritten extra child-plan list into an already copied node. */
static void
set_additional_child_plans(Plan *plan, List *children)
{
	switch (nodeTag(plan))
	{
	case T_Append:
		((Append *)plan)->appendplans = children;
		break;
	case T_MergeAppend:
		((MergeAppend *)plan)->mergeplans = children;
		break;
	case T_BitmapAnd:
		((BitmapAnd *)plan)->bitmapplans = children;
		break;
	case T_BitmapOr:
		((BitmapOr *)plan)->bitmapplans = children;
		break;
	case T_CustomScan:
		((CustomScan *)plan)->custom_plans = children;
		break;
	default:
		break;
	}
}

/*
 * Rewrite expressions and children stored in the common Plan prefix, plus
 * known extra child containers.  Node-specific expression fields remain
 * unchanged.  A concrete node is copied only after one field changes.
 */
Plan *
jsonb_slice_plan_rewrite_plan(Plan *plan, RewriteState *state)
{
	List *target_list;
	List *filter;
	List *init_plans;
	List *original_additional_children;
	List *additional_children;
	Plan *left_child;
	Plan *right_child;
	Plan *original_subquery_plan = NULL;
	Plan *subquery_plan = NULL;
	Plan *copy;
	Size node_size;

	if (plan == NULL)
		return NULL;
	check_stack_depth();
	node_size = plan_node_size(nodeTag(plan));
	if (node_size == 0)
		return plan;

	target_list = (List *)rewrite_expression((Node *)plan->targetlist, state);
	filter = (List *)rewrite_expression((Node *)plan->qual, state);
	init_plans = (List *)rewrite_expression((Node *)plan->initPlan, state);
	left_child = jsonb_slice_plan_rewrite_plan(plan->lefttree, state);
	right_child = jsonb_slice_plan_rewrite_plan(plan->righttree, state);
	original_additional_children = additional_child_plans(plan);
	additional_children = jsonb_slice_plan_rewrite_plan_list(
		original_additional_children, state);
	if (IsA(plan, SubqueryScan))
	{
		original_subquery_plan = ((SubqueryScan *)plan)->subplan;
		subquery_plan =
			jsonb_slice_plan_rewrite_plan(original_subquery_plan, state);
	}

	if (target_list == plan->targetlist && filter == plan->qual &&
		init_plans == plan->initPlan && left_child == plan->lefttree &&
		right_child == plan->righttree &&
		additional_children == original_additional_children &&
		subquery_plan == original_subquery_plan)
		return plan;

	copy = (Plan *)memcpy(palloc(node_size), plan, node_size);
	copy->targetlist = target_list;
	copy->qual = filter;
	copy->initPlan = init_plans;
	copy->lefttree = left_child;
	copy->righttree = right_child;
	set_additional_child_plans(copy, additional_children);
	if (IsA(copy, SubqueryScan))
		((SubqueryScan *)copy)->subplan = subquery_plan;
	return copy;
}
