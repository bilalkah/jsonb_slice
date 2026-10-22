# Install 0.2.0

0.2.0 is a PostgreSQL C extension. It provides one function:
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

The version must be `0.2.0`. The extension is relocatable.

## Upgrade from 0.1.0

Install the 0.2.0 files. Then run:

```sql
ALTER EXTENSION jsonb_slice UPDATE TO '0.2.0';
```

The upgrade adds the accessor and keeps the extension identity.

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

The test checks a fresh installation, the accessor rules, extension removal,
and an upgrade from 0.1.0.

## Current boundary

0.2.0 detoasts the complete JSONB value before it reads the object directory.
It does not reduce TOAST reads and makes no performance claim.
