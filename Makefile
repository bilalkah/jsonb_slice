EXTENSION = jsonb_slice
DATA = sql/jsonb_slice--0.1.0.sql

REGRESS = install
REGRESS_OPTS = --inputdir=test

PG_CONFIG ?= pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
