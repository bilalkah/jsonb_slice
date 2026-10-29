/*
 * jsonb_slice_plan_functions.c
 *
 * Function identity and permission state used by transparent planning.
 * PostgreSQL assigns extension-function OIDs per database, so this module
 * finds and validates them instead of trusting names or search_path.
 */
#include "postgres.h"

#include "access/htup_details.h"
#include "catalog/dependency.h"
#include "catalog/pg_language.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_type.h"
#include "commands/extension.h"
#include "nodes/makefuncs.h"
#include "nodes/primnodes.h"
#include "utils/acl.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/syscache.h"

#include "jsonb_slice_plan.h"
#include "jsonb_slice_plan_internal.h"

typedef struct AccessorSpec
{
	Oid builtin_oid;
	const char *sql_name;
	const char *c_symbol;
	Oid second_arg_type;
	Oid result_type;
} AccessorSpec;

typedef enum AccessorIndex
{
	ACCESSOR_OBJECT_FIELD,
	ACCESSOR_ARRAY_ELEMENT,
	ACCESSOR_OBJECT_FIELD_TEXT,
	ACCESSOR_ARRAY_ELEMENT_TEXT,
	ACCESSOR_EXTRACT_PATH,
	ACCESSOR_EXTRACT_PATH_TEXT,
	ACCESSOR_COUNT = JSONB_SLICE_ACCESSOR_COUNT
} AccessorIndex;

/*
 * One row is the fixed contract between a PostgreSQL accessor and its
 * jsonb_slice replacement.  The row index also selects the runtime OID and
 * permission answers stored below.
 */
static const AccessorSpec accessor_specs[ACCESSOR_COUNT] = {
	[ACCESSOR_OBJECT_FIELD] = {F_JSONB_OBJECT_FIELD, "jsonb_slice_get",
							   "jsonb_slice_get", TEXTOID, JSONBOID},
	[ACCESSOR_ARRAY_ELEMENT] = {F_JSONB_ARRAY_ELEMENT, "jsonb_slice_get",
								"jsonb_slice_get_index", INT4OID, JSONBOID},
	[ACCESSOR_OBJECT_FIELD_TEXT] = {F_JSONB_OBJECT_FIELD_TEXT,
									"jsonb_slice_get_text",
									"jsonb_slice_get_text_key", TEXTOID,
									TEXTOID},
	[ACCESSOR_ARRAY_ELEMENT_TEXT] = {F_JSONB_ARRAY_ELEMENT_TEXT,
									 "jsonb_slice_get_text",
									 "jsonb_slice_get_text_index", INT4OID,
									 TEXTOID},
	[ACCESSOR_EXTRACT_PATH] = {F_JSONB_EXTRACT_PATH, "jsonb_slice_path",
							   "jsonb_slice_path", TEXTARRAYOID, JSONBOID},
	[ACCESSOR_EXTRACT_PATH_TEXT] = {F_JSONB_EXTRACT_PATH_TEXT,
									"jsonb_slice_path_text",
									"jsonb_slice_path_text", TEXTARRAYOID,
									TEXTOID},
};

/*
 * These arrays contain backend-local database state.  A pg_proc invalidation
 * marks the complete set stale so the next planned statement rebuilds it.
 */
static Oid replacement_function_oids[ACCESSOR_COUNT];
static bool replacement_public_execute[ACCESSOR_COUNT];
static bool builtin_public_execute[ACCESSOR_COUNT];
static bool function_state_valid = false;
static uint64 function_state_generation = 0;

static void
functions_changed(Datum argument,
#if PG_VERSION_NUM >= 190000
				  SysCacheIdentifier cache_id,
#else
				  int cache_id,
#endif
				  uint32 hash_value)
{
	function_state_valid = false;
	function_state_generation++;
}

/*
 * Return true only when PUBLIC has EXECUTE.  This conservative rule keeps a
 * cached plan valid across SET ROLE because its answer does not depend on the
 * role that happened to plan the statement.
 */
static bool
public_can_execute(HeapTuple function_tuple)
{
	Form_pg_proc function_form;
	Datum acl_datum;
	Acl *acl;
	AclItem *items;
	bool is_null;
	bool executable = false;
	int item_index;

	function_form = (Form_pg_proc)GETSTRUCT(function_tuple);
	acl_datum = SysCacheGetAttr(PROCOID, function_tuple, Anum_pg_proc_proacl,
								&is_null);
	if (is_null)
		acl = acldefault(OBJECT_FUNCTION, function_form->proowner);
	else
		acl = DatumGetAclPCopy(acl_datum);

	items = ACL_DAT(acl);
	for (item_index = 0; item_index < ACL_NUM(acl); item_index++)
	{
		if (items[item_index].ai_grantee == ACL_ID_PUBLIC &&
			(ACLITEM_GET_PRIVS(items[item_index]) & ACL_EXECUTE) != 0)
		{
			executable = true;
			break;
		}
	}

	pfree(acl);
	return executable;
}

/* Read and compare one text field from a cached pg_proc row. */
static bool
text_attribute_matches(HeapTuple function_tuple, AttrNumber attribute_number,
					   const char *expected)
{
	Datum attribute;
	char *actual;
	bool is_null;
	bool matches;

	attribute =
		SysCacheGetAttr(PROCOID, function_tuple, attribute_number, &is_null);
	if (is_null)
		return false;

	actual = TextDatumGetCString(attribute);
	matches = strcmp(actual, expected) == 0;
	pfree(actual);
	return matches;
}

/*
 * Prove that one catalog function is still the exact function installed by
 * this extension.  Identity and PUBLIC permission are returned separately.
 */
static bool
replacement_matches(Oid function_oid, Oid extension_oid, Oid schema_oid,
					const AccessorSpec *spec, bool *public_execute)
{
	HeapTuple function_tuple;
	Form_pg_proc function_form;
	bool matches;

	*public_execute = false;
	if (!OidIsValid(function_oid) ||
		getExtensionOfObject(ProcedureRelationId, function_oid) !=
			extension_oid)
		return false;

	function_tuple = SearchSysCache1(PROCOID, ObjectIdGetDatum(function_oid));
	if (!HeapTupleIsValid(function_tuple))
		return false;

	function_form = (Form_pg_proc)GETSTRUCT(function_tuple);
	matches = function_form->pronamespace == schema_oid &&
			  function_form->prokind == PROKIND_FUNCTION &&
			  function_form->prolang == ClanguageId &&
			  function_form->pronargs == 2 &&
			  function_form->proargtypes.values[0] == JSONBOID &&
			  function_form->proargtypes.values[1] == spec->second_arg_type &&
			  !OidIsValid(function_form->provariadic) &&
			  function_form->proisstrict &&
			  function_form->provolatile == PROVOLATILE_IMMUTABLE &&
			  function_form->proparallel == PROPARALLEL_SAFE &&
			  !function_form->proretset && !function_form->prosecdef &&
			  function_form->prorettype == spec->result_type &&
			  heap_attisnull(function_tuple, Anum_pg_proc_proconfig, NULL) &&
			  text_attribute_matches(function_tuple, Anum_pg_proc_prosrc,
									 spec->c_symbol) &&
			  text_attribute_matches(function_tuple, Anum_pg_proc_probin,
									 "$libdir/jsonb_slice");

	if (matches)
		*public_execute = public_can_execute(function_tuple);
	ReleaseSysCache(function_tuple);
	return matches;
}

/* Find one overload in the extension schema, never through search_path. */
static Oid
find_replacement(Oid extension_oid, Oid schema_oid, const AccessorSpec *spec,
				 bool *public_execute)
{
	Oid argument_types[2] = {JSONBOID, spec->second_arg_type};
	oidvector *argument_vector;
	Oid function_oid;

	argument_vector = buildoidvector(argument_types, lengthof(argument_types));
	function_oid = GetSysCacheOid3(
		PROCNAMEARGSNSP, Anum_pg_proc_oid, CStringGetDatum(spec->sql_name),
		PointerGetDatum(argument_vector), ObjectIdGetDatum(schema_oid));
	pfree(argument_vector);

	if (!replacement_matches(function_oid, extension_oid, schema_oid, spec,
							 public_execute))
		return InvalidOid;
	return function_oid;
}

/* Rebuild one complete and internally consistent function-state snapshot. */
static void
refresh_function_state(void)
{
	Oid found_oids[ACCESSOR_COUNT] = {InvalidOid};
	bool found_public_execute[ACCESSOR_COUNT] = {false};
	bool found_builtin_execute[ACCESSOR_COUNT] = {false};
	uint64 start_generation = function_state_generation;
	Oid extension_oid;
	Oid schema_oid;
	int accessor_index;

	extension_oid = get_extension_oid("jsonb_slice", true);
	schema_oid = OidIsValid(extension_oid)
					 ? get_extension_schema(extension_oid)
					 : InvalidOid;

	for (accessor_index = 0;
		 OidIsValid(schema_oid) && accessor_index < ACCESSOR_COUNT;
		 accessor_index++)
		found_oids[accessor_index] = find_replacement(
			extension_oid, schema_oid, &accessor_specs[accessor_index],
			&found_public_execute[accessor_index]);

	for (accessor_index = 0; accessor_index < ACCESSOR_COUNT; accessor_index++)
	{
		HeapTuple function_tuple = SearchSysCache1(
			PROCOID,
			ObjectIdGetDatum(accessor_specs[accessor_index].builtin_oid));

		if (HeapTupleIsValid(function_tuple))
		{
			found_builtin_execute[accessor_index] =
				public_can_execute(function_tuple);
			ReleaseSysCache(function_tuple);
		}
	}

	memcpy(replacement_function_oids, found_oids, sizeof(found_oids));
	memcpy(replacement_public_execute, found_public_execute,
		   sizeof(found_public_execute));
	memcpy(builtin_public_execute, found_builtin_execute,
		   sizeof(found_builtin_execute));
	function_state_valid = function_state_generation == start_generation;
}

void
jsonb_slice_plan_ensure_function_state(void)
{
	while (!function_state_valid)
		refresh_function_state();
}

/* Return the two SQL arguments stored by either supported call-node type. */
static List *
call_arguments(Node *node)
{
	if (IsA(node, OpExpr))
		return ((OpExpr *)node)->args;
	return ((FuncExpr *)node)->args;
}

/* Return the accessor-table index for a built-in OpExpr or FuncExpr. */
static int
builtin_accessor_called(Node *node)
{
	Oid function_oid;
	int accessor_index;

	if (IsA(node, OpExpr))
	{
		OpExpr *operator_expression = (OpExpr *)node;

		function_oid = OidIsValid(operator_expression->opfuncid)
						   ? operator_expression->opfuncid
						   : get_opcode(operator_expression->opno);
	}
	else if (IsA(node, FuncExpr))
		function_oid = ((FuncExpr *)node)->funcid;
	else
		return -1;

	if (list_length(call_arguments(node)) != 2)
		return -1;

	for (accessor_index = 0; accessor_index < ACCESSOR_COUNT; accessor_index++)
		if (function_oid == accessor_specs[accessor_index].builtin_oid)
			return accessor_index;
	return -1;
}

/*
 * Return the validated replacement OID when one built-in call can be changed.
 * rewrite_statement() invokes the walker only while transparent mode is on.
 * Both function dependencies are recorded before a permission rejection so a
 * later GRANT or REVOKE can invalidate a cached negative decision.
 */
bool
jsonb_slice_plan_replacement(Node *node, RewriteState *state,
							 Oid *replacement_oid)
{
	int accessor_index = builtin_accessor_called(node);

	*replacement_oid = InvalidOid;
	if (accessor_index < 0)
		return false;
	if (!OidIsValid(replacement_function_oids[accessor_index]))
		return false;

	state->replacement_dependency[accessor_index] = true;
	state->builtin_dependency[accessor_index] = true;
	if (!replacement_public_execute[accessor_index] ||
		!builtin_public_execute[accessor_index])
		return false;

	*replacement_oid = replacement_function_oids[accessor_index];
	return true;
}

static PlanInvalItem *
function_dependency(Oid function_oid)
{
	PlanInvalItem *dependency = makeNode(PlanInvalItem);

	dependency->cacheId = PROCOID;
	dependency->hashValue =
		GetSysCacheHashValue1(PROCOID, ObjectIdGetDatum(function_oid));
	return dependency;
}

bool
jsonb_slice_plan_rewrite_consulted_function(const RewriteState *state)
{
	int accessor_index;

	for (accessor_index = 0; accessor_index < ACCESSOR_COUNT; accessor_index++)
		if (state->replacement_dependency[accessor_index] ||
			state->builtin_dependency[accessor_index])
			return true;
	return false;
}

void
jsonb_slice_plan_add_function_dependencies(PlannedStmt *statement,
										   const RewriteState *state)
{
	int accessor_index;

	statement->invalItems = list_copy(statement->invalItems);
	for (accessor_index = 0; accessor_index < ACCESSOR_COUNT; accessor_index++)
	{
		if (state->replacement_dependency[accessor_index])
			statement->invalItems =
				lappend(statement->invalItems,
						function_dependency(
							replacement_function_oids[accessor_index]));
		if (state->builtin_dependency[accessor_index])
			statement->invalItems =
				lappend(statement->invalItems,
						function_dependency(
							accessor_specs[accessor_index].builtin_oid));
	}
}

void
jsonb_slice_plan_functions_init(void)
{
	CacheRegisterSyscacheCallback(PROCOID, functions_changed, (Datum)0);
}

/* Test-only stable counts; database-specific OIDs remain private. */
void
jsonb_slice_plan_test_function_state(int *validated_count,
									 int *replacement_public_count,
									 int *builtin_public_count)
{
	int accessor_index;

	jsonb_slice_plan_ensure_function_state();
	*validated_count = 0;
	*replacement_public_count = 0;
	*builtin_public_count = 0;
	for (accessor_index = 0; accessor_index < ACCESSOR_COUNT; accessor_index++)
	{
		if (OidIsValid(replacement_function_oids[accessor_index]))
			(*validated_count)++;
		if (replacement_public_execute[accessor_index])
			(*replacement_public_count)++;
		if (builtin_public_execute[accessor_index])
			(*builtin_public_count)++;
	}
}
