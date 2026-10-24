# The problem

A PostgreSQL row lives in a database page. A large value does not fit well in
that row. PostgreSQL can compress the value and move it to a TOAST table.

The TOAST table stores the value in numbered chunks. The table row keeps a
small pointer to those chunks.

## A normal field read

PostgreSQL's JSONB operators work on an in-memory JSONB value. PostgreSQL
restores a large value from TOAST before an operator reads it. PostgreSQL also
decompresses a compressed value.

```text
table row -> all TOAST chunks -> complete jsonb -> one field
```

This behavior is general and correct. Its cost can be high when a query needs
one small field from a large document.

## The 0.3.0 read

0.3.0 checks the storage form before PostgreSQL detoasts the input. The
function uses TOAST slices for an external, uncompressed value.

```text
external jsonb -> root directory and keys -> selected value -> result
```

The function reads the root header first. It then reads the top-level entry
directory and key bytes. A binary search finds the requested key. The function
then reads the bytes for the selected value.

Inline and compressed values use the normal complete-value path from 0.2.0.
The result rules stay the same for both paths.

## Current result

0.3.0 can read part of an external, uncompressed JSONB value. In the regression
test, the requested slice lengths total less than the complete test value. The
release makes no general performance claim.
