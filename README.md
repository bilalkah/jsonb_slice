# jsonb_slice

jsonb_slice is a PostgreSQL extension experiment. It asks one question:

> Can PostgreSQL read one field from a large `jsonb` value without reading the
> complete value?

## Version 0.1.0

Version 0.1.0 is an installable extension shell. It establishes the extension
name and package layout. It has no JSONB accessor, shared library, or
performance claim.

Install PostgreSQL development files first. Then run:

```sh
make
make install
make installcheck
```

Create the extension in a test database:

```sql
CREATE EXTENSION jsonb_slice;

SELECT extname, extversion
FROM pg_extension
WHERE extname = 'jsonb_slice';
```

The result identifies version `0.1.0`. This version does not create a
function, operator, background worker, or planner hook.

This project is an experiment and a study. Do not use this version in a
production database.

The project uses the [PostgreSQL License](LICENSE).
