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

## The 0.4.0 read

0.4.0 checks the storage form before PostgreSQL detoasts the input. The
accessors use TOAST slices for an external, uncompressed value.

```text
external jsonb -> visited container directories -> selected value -> result
```

The reader starts with the root container. An object step reads that object's
directory and key bytes. A binary search finds the requested key. An array
step reads the array directory and locates the requested index. A nested path
repeats these steps until it reaches the selected value.

The reader does not fetch sibling value payloads during traversal. It reads
the complete selected value after the final step. Inline and compressed
values use PostgreSQL's complete-value functions. The result rules stay the
same for both paths.

## Current result

0.4.0 can read part of an external, uncompressed JSONB value for key, index,
and path accessors. In selected regression fixtures, the requested slice sizes
total less than the stored test value. The release makes no general
performance claim.
