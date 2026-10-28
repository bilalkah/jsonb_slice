# Install 0.4.0

0.4.0 is a PostgreSQL C extension. It provides key, index, and path accessors
with JSONB and text results.

## Requirements

You need:

- PostgreSQL with its development files;
- a C compiler;
- GNU Make;
- permission to install files in the PostgreSQL extension directories;
- a test database where you can create an extension.

`pg_config` must refer to the PostgreSQL installation that loads the
extension.

## Build and install

Run these commands from the repository root:

```sh
make PG_CONFIG=/path/to/pg_config
make install PG_CONFIG=/path/to/pg_config
```

You can omit `PG_CONFIG` when the correct command is already on `PATH`.

## Create the extension

Connect to a test database and run:

```sql
CREATE EXTENSION jsonb_slice;

SELECT extname, extversion, extrelocatable
FROM pg_extension
WHERE extname = 'jsonb_slice';
```

The version must be `0.4.0`. The extension is relocatable.

## Upgrade from 0.3.0

Install the 0.4.0 files. Then run:

```sql
ALTER EXTENSION jsonb_slice UPDATE TO '0.4.0';
```

The upgrade adds the new function signatures and operators. It does not change
stored JSONB values.

## Read by key

```sql
SELECT jsonb_slice_get(
    '{"name": "Ada", "active": true}'::jsonb,
    'name'
);
```

The result is the JSONB string `"Ada"`.

Use `jsonb_slice_get_text` for an SQL text result:

```sql
SELECT jsonb_slice_get_text('{"name": "Ada"}'::jsonb, 'name');
```

The result is `Ada`.

## Read by array index

```sql
SELECT jsonb_slice_get('[10, 20, 30]'::jsonb, 1);
SELECT jsonb_slice_get_text('["first", "last"]'::jsonb, -1);
```

The first result is JSONB `20`. The second result is the text `last`. A
negative index counts from the end.

## Read a nested path

```sql
SELECT jsonb_slice_path(
    '{"users": [{"name": "Ada"}]}'::jsonb,
    ARRAY['users', '0', 'name']
);

SELECT jsonb_slice_path_text(
    '{"users": [{"name": "Ada"}]}'::jsonb,
    ARRAY['users', '0', 'name']
);
```

The first result is the JSONB string `"Ada"`. The second result is the text
`Ada`.

## Use the operators

The extension operators call the same functions:

```sql
SELECT '{"name": "Ada"}'::jsonb ~> 'name';
SELECT '["first", "last"]'::jsonb ~>> -1;
SELECT '{"users": [{"name": "Ada"}]}'::jsonb
       ~#>> ARRAY['users', '0', 'name'];
```

The `~>` and `~#>` operators return JSONB. The `~>>` and `~#>>` operators
return SQL text.

## Result rules

- A missing key, index, or path returns SQL `NULL`.
- A JSONB result preserves JSON `null`.
- A text result converts JSON `null` to SQL `NULL`.
- A path can move through objects and arrays.
- A path that continues through a scalar returns SQL `NULL`.
- A SQL `NULL` argument returns SQL `NULL`.

## Run the regression tests

The PostgreSQL server must be running. The current operating-system user must
be able to create an extension in the test database.

```sh
make installcheck PG_CONFIG=/path/to/pg_config
```

The tests check a fresh installation, all six accessor signatures, all six
operator signatures, storage fallbacks, extension removal, and upgrades from
0.1.0 through 0.4.0.

## Current boundary

0.4.0 uses TOAST slices for an external, uncompressed JSONB value. It reads
the directory of each visited container, the required object key bytes, and
the final selected value. Inline or compressed input uses PostgreSQL's
complete-value functions. This release makes no general performance claim.
