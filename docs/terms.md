# Terms

## SQL and C interface

### `Datum`

The general C type that PostgreSQL uses to pass SQL values. A small value can
fit in the `Datum`. A variable-length value normally uses the `Datum` as a
pointer.

### `fcinfo`

The information for one PostgreSQL function call. It contains the arguments,
null flags, result information, and other call data. `PG_FUNCTION_ARGS`
declares this value for a C function.

### `PG_GETARG_POINTER`

A macro that returns the raw pointer from a function argument. It does not
detoast the value. The extension uses it to inspect the current storage form
before it selects a read path.

### `PG_GETARG_JSONB_P`

A macro that returns a usable `Jsonb *`. PostgreSQL detoasts and decompresses
the argument when this is necessary. This can read the complete value.

### SQL null

The absence of an SQL value. PostgreSQL does not call a `STRICT` function when
an argument is SQL null.

### JSON null

A real JSON value whose JSON type is null. It is different from SQL null.

## Variable-length values

### `varlena`

The common PostgreSQL representation for variable-length values, such as
`text` and `jsonb`. A normal value starts with a four-byte header. A raw
`varlena` pointer can also identify a short value, a compressed value, or an
external value.

### varlena header

The prefix that identifies the `varlena` form and its size. PostgreSQL code
must use the varlena macros to read it.

| Form | Prefix before its data |
| --- | ---: |
| Normal inline value | 4 bytes |
| Short inline value | 1 byte |
| Compressed inline value | 8 bytes |
| On-disk external pointer | 2 bytes before the pointer metadata |

The compressed form has a four-byte varlena header and a four-byte compression
information field. An on-disk TOAST pointer normally has a two-byte prefix and
16 bytes of pointer metadata. It does not contain the value data.

### logical data

The uncompressed bytes that form the SQL value. A TOAST slice offset starts in
this data, after the original varlena header. For `jsonb`, offset zero is the
root container header.

### stored size

The number of bytes in the current stored form. Compression can make the
stored size smaller than the logical size.

## TOAST storage

### TOAST

PostgreSQL's system for values that are too large for normal row storage. It
can compress a value or move it to a secondary table in chunks.

### inline value

A value that is present in the main table row. It can be uncompressed or
compressed.

### compressed value

A value whose data uses a PostgreSQL compression method. Compression does not
show whether the value is in memory or on disk. A value can be compressed
inline or compressed in a TOAST table.

### external value

A value that PostgreSQL stores outside the main table row. The table row keeps
a small pointer to the value. An external value can be compressed or
uncompressed.

### TOAST pointer

Metadata that identifies an external value. An on-disk TOAST pointer records
the logical size, stored size and compression information, value identifier,
and TOAST table identifier. It does not contain the JSONB data.

### TOAST chunk

One numbered part of an external value in a TOAST table.

### TOAST slice

A requested byte range from a TOAST value. For an external, uncompressed
value, PostgreSQL reads the TOAST chunks that cover the range.

### detoast

To make the logical value available from its current TOAST form. This can
include reading all chunks and decompressing the value.

### storage representation

The current form of one stored value: inline, compressed, or external.
PostgreSQL macros inspect this form. They do not calculate whether the value
could fit in the row.

### storage strategy

The column setting that tells TOAST which storage operations it can use.

| Strategy | Behavior |
| --- | --- |
| `PLAIN` | Keep the value inline. Do not compress it. |
| `MAIN` | Allow compression. Prefer inline storage. |
| `EXTERNAL` | Allow external storage. Do not compress it. |
| `EXTENDED` | Allow compression and external storage. |

### `VARATT_IS_EXTERNAL_ONDISK`

A macro that reports whether a raw value is an on-disk TOAST pointer. It
inspects the current representation. It does not test whether the complete
value can fit in a row.

### `VARATT_EXTERNAL_IS_COMPRESSED`

A macro that reports whether the value behind an on-disk TOAST pointer is
compressed.

## JSONB layout

### `jsonb`

PostgreSQL's binary representation of JSON data. It stores a container
directory before the container data.

### JSONB container header

The first four bytes of a JSONB container. It records the container type and
item count. It is part of the logical JSONB data. It is not the varlena
header.

### `JEntry`

A four-byte JSONB directory entry. It records a value type and either a length
or an end offset. An object uses one key entry and one value entry for each
field.

### JSONB object directory

The `JEntry` array at the start of a JSONB object. The key entries come first.
The value entries follow them. The key and value bytes come after the complete
directory.

## Project structures

### `SliceSource`

The per-call context for partial reads. It keeps the raw external value, the
logical read boundary, and the byte counter used by the regression test. It is
a local C structure. It is not stored in PostgreSQL.

### `ValueLocation`

The decoded location of one JSONB value. It keeps the JEntry type, absolute
slice offset, and requested length. This lets the code validate a location
before it fetches and rebuilds the result.
