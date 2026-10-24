# Test contract

0.2.0 has one regression test. It checks these areas:

- a fresh 0.2.0 installation;
- the C function's volatility, strictness, and parallel-safety flags;
- ownership of the function by the extension;
- results for each JSONB value type;
- missing, empty, and multibyte field names;
- non-object and SQL null inputs;
- sliced reads from an external, uncompressed table value;
- proof that a sliced read fetches fewer bytes than the complete value;
- full-detoast fallback for inline and compressed values;
- removal of the function by `DROP EXTENSION`;
- an extension upgrade from 0.1.0 to 0.2.0.

Each accessor case compares `jsonb_slice_get(document, field_name)` with
`document -> field_name`.

The test does not make a partial-read or performance claim.
