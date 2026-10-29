/*
 * jsonb_slice_plan_relations.c
 *
 * Statement-level relation eligibility for transparent planning.  The cache
 * stores table declarations, while each accessor still checks the actual
 * stored value before it chooses a sliced read or PostgreSQL fallback.
 */
#include "postgres.h"

#include "catalog/pg_class.h"
#include "catalog/pg_type.h"
#include "nodes/parsenodes.h"
#include "utils/hsearch.h"
#include "utils/inval.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/rel.h"
#include "utils/relcache.h"

#include "jsonb_slice_plan.h"
#include "jsonb_slice_plan_internal.h"

typedef struct RelationEligibility
{
	Oid relation_oid;
	bool has_external_jsonb;
} RelationEligibility;

/* Backend-local relation answers, invalidated by PostgreSQL's relcache. */
static HTAB *relation_eligibility_cache = NULL;

/* Test-only evidence from the most recently planned statement. */
static bool last_statement_eligible = false;

static bool
relation_kind_is_supported(char relation_kind)
{
	return relation_kind == RELKIND_RELATION ||
		   relation_kind == RELKIND_PARTITIONED_TABLE ||
		   relation_kind == RELKIND_MATVIEW;
}

/* Remove one stale relation answer, or the complete cache when required. */
static void
relations_changed(Datum argument, Oid relation_oid)
{
	if (relation_eligibility_cache == NULL)
		return;

	if (OidIsValid(relation_oid))
		(void)hash_search(relation_eligibility_cache, &relation_oid,
						  HASH_REMOVE, NULL);
	else
	{
		hash_destroy(relation_eligibility_cache);
		relation_eligibility_cache = NULL;
	}
}

static void
create_relation_eligibility_cache(void)
{
	HASHCTL control;

	memset(&control, 0, sizeof(control));
	control.keysize = sizeof(Oid);
	control.entrysize = sizeof(RelationEligibility);
	control.hcxt = TopMemoryContext;
	relation_eligibility_cache =
		hash_create("jsonb_slice relation eligibility", 32, &control,
					HASH_ELEM | HASH_BLOBS | HASH_CONTEXT);
}

/*
 * Return whether one supported relation has a non-dropped JSONB column whose
 * declared storage is EXTERNAL.  Domains over JSONB use their base type.
 */
static bool
relation_has_external_jsonb(Oid relation_oid)
{
	RelationEligibility *entry;
	Relation relation;
	TupleDesc descriptor;
	bool eligible = false;
	int attribute_index;

	if (relation_eligibility_cache != NULL)
	{
		entry = hash_search(relation_eligibility_cache, &relation_oid,
							HASH_FIND, NULL);
		if (entry != NULL)
			return entry->has_external_jsonb;
	}

	relation = RelationIdGetRelation(relation_oid);
	if (!RelationIsValid(relation))
		return false;

	if (relation_kind_is_supported(RelationGetForm(relation)->relkind))
	{
		descriptor = RelationGetDescr(relation);
		for (attribute_index = 0; attribute_index < descriptor->natts;
			 attribute_index++)
		{
			Form_pg_attribute attribute =
				TupleDescAttr(descriptor, attribute_index);

			if (!attribute->attisdropped &&
				attribute->attstorage == TYPSTORAGE_EXTERNAL &&
				getBaseType(attribute->atttypid) == JSONBOID)
			{
				eligible = true;
				break;
			}
		}
	}
	RelationClose(relation);

	if (relation_eligibility_cache == NULL)
		create_relation_eligibility_cache();
	entry = hash_search(relation_eligibility_cache, &relation_oid, HASH_ENTER,
						NULL);
	entry->has_external_jsonb = eligible;
	return eligible;
}

/* One eligible range-table relation opens the statement-wide planner gate. */
static bool
statement_reads_external_jsonb(List *range_table)
{
	ListCell *cell;

	foreach (cell, range_table)
	{
		RangeTblEntry *entry = lfirst_node(RangeTblEntry, cell);

		if (entry->rtekind == RTE_RELATION &&
			relation_kind_is_supported(entry->relkind) &&
			relation_has_external_jsonb(entry->relid))
			return true;
	}
	return false;
}

bool
jsonb_slice_plan_statement_is_eligible(List *range_table)
{
	last_statement_eligible = statement_reads_external_jsonb(range_table);
	return last_statement_eligible;
}

void
jsonb_slice_plan_relations_init(void)
{
	CacheRegisterRelcacheCallback(relations_changed, (Datum)0);
}

bool
jsonb_slice_plan_test_statement_eligible(void)
{
	return last_statement_eligible;
}
