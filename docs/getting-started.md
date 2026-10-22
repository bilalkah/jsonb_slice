# Install 0.1.0

0.1.0 is an installable PostgreSQL extension shell. It records the project
name and version in PostgreSQL. It does not read JSONB values.

## Requirements

You need:

- PostgreSQL with its development files;
- GNU Make;
- permission to install files in the PostgreSQL extension directory;
- a test database where you can create an extension.

`pg_config` must refer to the PostgreSQL installation that loads the extension.

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

The version must be `0.1.0`. The extension is relocatable because
0.1.0 creates no schema objects.

## Run the package test

The PostgreSQL server must be running. The current operating-system user must
be able to create an extension in the test database.

```sh
make installcheck PG_CONFIG=/path/to/pg_config
```

The test creates the extension, checks its metadata, and drops it.

## Current boundary

0.1.0 creates no functions or operators. A successful installation does not
change a query plan or JSONB result.
