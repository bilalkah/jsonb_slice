\echo Use "ALTER EXTENSION jsonb_slice UPDATE TO '0.3.0'" to load this file. \quit

COMMENT ON FUNCTION jsonb_slice_get(jsonb, text) IS
'read one top-level field with TOAST slices when the stored jsonb value permits it';

COMMENT ON EXTENSION jsonb_slice IS
'Study partial reads of large jsonb values; 0.3.0 adds a sliced read path';
