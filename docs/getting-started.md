# Install 0.5.0

0.5.0 is a PostgreSQL C extension. It provides key, index, and path accessors
with JSONB and text results. It also provides an optional transparent mode for
supported built-in accessor expressions.

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

The version must be `0.5.0`. The extension is relocatable.

## Upgrade from 0.4.0

Install the 0.5.0 files. Then run:

```sql
ALTER EXTENSION jsonb_slice UPDATE TO '0.5.0';
```

The upgrade keeps the existing function and operator signatures. It updates
the extension version and makes the new library behavior available. It does
not change stored JSONB values.

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

## Enable transparent mode

The planner hook must be loaded before PostgreSQL plans the query. Load it in
the current session, and then enable transparent mode:

```sql
LOAD 'jsonb_slice';
SET jsonb_slice.transparent = on;
```

Existing built-in syntax can then use the matching extension accessor in a
supported plan expression:

```sql
SELECT document -> 'profile',
       document ->> 'name',
       document #>> ARRAY['profile', 'role']
FROM records;
```

The setting is off by default. Reset it with:

```sql
RESET jsonb_slice.transparent;
```

To load the library for each new session, set this PostgreSQL configuration:

```conf
session_preload_libraries = 'jsonb_slice'
```

Start a new session after the configuration change. `CREATE EXTENSION` alone
does not load the planner hook in every session.

### Transparent-mode scope

The mode inspects accessors in:

- common select lists;
- ordinary filters;
- common left and right child plans;
- append-style child plans;
- scalar and other planned subplans.

If an accessor is stored in another plan-specific field, PostgreSQL keeps the
built-in function. The query result does not change. The call only misses the
partial-read opportunity.

A statement is considered only when it reads a normal table, partitioned
table, or materialized view with a JSONB column declared as `STORAGE
EXTERNAL`. The stored value still controls execution. Inline and compressed
values use PostgreSQL's complete-value path.

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
operator signatures, transparent replacement, storage fallbacks, extension
removal, and upgrades from 0.1.0 through 0.5.0.

## Current boundary

0.5.0 uses TOAST slices for an external, uncompressed JSONB value. It reads
the directory of each visited container, the required object key bytes, and
the final selected value. Inline or compressed input uses PostgreSQL's
complete-value functions. This release makes no general performance claim.
