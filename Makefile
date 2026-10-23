EXTENSION = jsonb_slice
MODULE_big = jsonb_slice
OBJS = src/jsonb_slice.o

DATA = \
	sql/jsonb_slice--0.1.0.sql \
	sql/jsonb_slice--0.2.0.sql \
	sql/jsonb_slice--0.1.0--0.2.0.sql

REGRESS = install
REGRESS_OPTS = --inputdir=test

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
