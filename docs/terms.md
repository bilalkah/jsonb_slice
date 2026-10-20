# Terms

## `jsonb`

PostgreSQL's binary representation of JSON data. It stores a container
directory before the container data.

## detoast

To restore a value from its TOAST storage form. This can include reading all
chunks and decompressing them.

## TOAST

PostgreSQL's system for values that are too large for normal row storage. It
can compress a value or move it to a secondary table in chunks.

## TOAST chunk

One numbered part of an out-of-line value in a TOAST table.
