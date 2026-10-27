EXTENSION = jsonb_slice
MODULE_big = jsonb_slice
OBJS = \
	src/jsonb_slice.o \
	src/jsonb_slice_core.o

DATA = \
	sql/jsonb_slice--0.1.0.sql \
	sql/jsonb_slice--0.2.0.sql \
	sql/jsonb_slice--0.3.0.sql \
	sql/jsonb_slice--0.4.0.sql \
	sql/jsonb_slice--0.1.0--0.2.0.sql \
	sql/jsonb_slice--0.2.0--0.3.0.sql \
	sql/jsonb_slice--0.3.0--0.4.0.sql

REGRESS = \
	install \
	get_key \
	get_index \
	get_text_key \
	get_text_index \
	get_path \
	get_path_text \
	operators \
	storage_fallback \
	upgrade
REGRESS_OPTS = --inputdir=test

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
