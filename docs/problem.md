# The problem

A PostgreSQL row lives in a database page. A large value does not fit well in
that row. PostgreSQL can compress the value and move it to a TOAST table.

The TOAST table stores the value in numbered chunks. The table row keeps a
small pointer to those chunks.

## A normal field read

PostgreSQL's `jsonb` operators work on the `jsonb` value. For a large value,
the database first restores the value from TOAST. If the value is compressed,
the database also decompresses it. The operator can then find the requested
field.

This behavior is general and correct. Its cost can be high when the query
needs a small field from a large document.

```text
table row -> TOAST pointer -> all stored chunks -> complete jsonb -> one field
```

## The research question

An uncompressed `jsonb` value has a binary directory before its data. The
directory describes the positions of its keys and values. A reader can use
that directory to find the byte range of one value.

The project asks whether a reader can use this path:

```text
table row -> TOAST pointer -> directory chunks -> value chunks -> one field
```

Version 0.1.0 does not implement this path. It contains the installable
extension shell and documents the question.

## The trade-off

Uncompressed storage can make a partial read possible. It also uses more disk
space and can write more data during an update. A complete evaluation must
account for three costs:

- read time and bytes read;
- table and index size;
- update time and write-ahead log volume.

Version 0.1.0 contains no benchmark result or performance claim.
