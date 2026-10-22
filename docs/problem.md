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

## The 0.2.0 read

0.2.0 establishes the accessor result. The function detoasts the complete
input and uses PostgreSQL's JSONB container lookup helper.

```text
jsonb input -> complete in-memory value -> object directory -> one field
```

The test compares every supported result with PostgreSQL's `->` operator.

## Current result

0.2.0 proves the function boundary, C library, JSONB lookup, package upgrade,
and result rules. It contains no partial-read or performance claim.
