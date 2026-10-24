# Install 0.3.0

0.3.0 is a PostgreSQL C extension. It provides one function:
`jsonb_slice_get(jsonb, text)`.

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

The version must be `0.3.0`. The extension is relocatable.

## Upgrade from 0.2.0

Install the 0.3.0 files. Then run:

```sql
ALTER EXTENSION jsonb_slice UPDATE TO '0.3.0';
```

The upgrade keeps the function signature and changes its read path.

## Read one field

```sql
SELECT jsonb_slice_get(
    '{"name": "Ada", "active": true}'::jsonb,
    'name'
);
```

The result is the JSONB string `"Ada"`.

The function has these rules:

- a stored field returns the same JSONB value as `document -> field_name`;
- a missing field returns SQL `NULL`;
- an array or scalar input returns SQL `NULL`;
- a stored JSON `null` returns JSONB `null`;
- a SQL `NULL` argument returns SQL `NULL`.

## Run the regression test

The PostgreSQL server must be running. The current operating-system user must
be able to create an extension in the test database.

```sh
make installcheck PG_CONFIG=/path/to/pg_config
```

The test checks a fresh installation, the accessor rules, the storage paths,
extension removal, and upgrades from 0.1.0 through 0.3.0.

## Current boundary

0.3.0 uses TOAST slices for an external, uncompressed JSONB value. It reads
the root directory, all top-level keys, and the selected field value. It uses
the normal complete-value lookup for inline or compressed input. This release
makes no performance claim.
