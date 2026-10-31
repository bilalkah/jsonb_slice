EXTENSION = jsonb_slice
MODULE_big = jsonb_slice
OBJS = \
	src/jsonb_slice.o \
	src/jsonb_slice_core.o \
	src/jsonb_slice_plan.o \
	src/jsonb_slice_plan_functions.o \
	src/jsonb_slice_plan_relations.o \
	src/jsonb_slice_plan_walk.o

DATA = \
	sql/jsonb_slice--0.1.0.sql \
	sql/jsonb_slice--0.2.0.sql \
	sql/jsonb_slice--0.3.0.sql \
	sql/jsonb_slice--0.4.0.sql \
	sql/jsonb_slice--0.5.0.sql \
	sql/jsonb_slice--0.1.0--0.2.0.sql \
	sql/jsonb_slice--0.2.0--0.3.0.sql \
	sql/jsonb_slice--0.3.0--0.4.0.sql \
	sql/jsonb_slice--0.4.0--0.5.0.sql

REGRESS = \
	install \
	get_key \
	get_index \
	get_text_key \
	get_text_index \
	get_path \
	get_path_text \
	operators \
	planner_config \
	function_validation \
	relation_eligibility \
	transparent_text_key \
	transparent_accessors \
	transparent_storage \
	plan_walker_common \
	storage_fallback \
	upgrade
REGRESS_OPTS = --inputdir=test

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
