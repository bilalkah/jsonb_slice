\echo Use "CREATE EXTENSION jsonb_slice" to load this file. \quit

-- 0.1.0 installs the extension identity only.
COMMENT ON EXTENSION jsonb_slice IS
'Study partial reads of large jsonb values; 0.1.0 has no accessors';
