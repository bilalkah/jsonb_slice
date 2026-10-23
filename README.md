# jsonb_slice

jsonb_slice is a PostgreSQL extension experiment. It asks one question:

> Can PostgreSQL read one field from a large `jsonb` value without reading the
> complete value?

PostgreSQL can move a large value to a TOAST table. It divides the value into
small chunks. The experiment studies when an extension can read only the
chunks that contain the requested field.

## Current release

`0.2.0` provides the first JSONB accessor. The C function
`jsonb_slice_get(jsonb, text)` reads one top-level object field. Its result
matches the result of PostgreSQL's `jsonb -> text` operator.

This version detoasts the complete JSONB value before it finds the field. It
does not perform a partial TOAST read and makes no performance claim.

Read the [versioned documentation](https://bilalkah.github.io/jsonb_slice/)
or the [0.2.0 release](https://github.com/bilalkah/jsonb_slice/releases/tag/v0.2.0).

## Repository map

| Path | Purpose in this release |
|---|---|
| `.clang-format` | PostgreSQL-style C layout for editors and CI |
| `Makefile` | PostgreSQL extension build and regression entry point |
| `jsonb_slice.control` | Extension identity and default version |
| `src/` | C source for the top-level key accessor |
| `sql/` | 0.1.0 and 0.2.0 installation and upgrade scripts |
| `docs/` | Versioned public documentation |
| `test/` | Installation, accessor, removal, and upgrade regression test |
| `landing/` | Documentation version index |
| `scripts/` | Versioned documentation build |
| `.github/workflows/` | Package checks, releases, and GitHub Pages deployment |

## Install 0.2.0

Install PostgreSQL development files first. Then run:

```sh
make
make install
make installcheck
```

Create the extension in a database:

```sql
CREATE EXTENSION jsonb_slice;

SELECT extname, extversion
FROM pg_extension
WHERE extname = 'jsonb_slice';
```

The result identifies version `0.2.0`.

Read one top-level object field:

```sql
SELECT jsonb_slice_get('{"name": "Ada", "active": true}'::jsonb, 'name');
```

The result is the JSONB string `"Ada"`. A missing field or a non-object input
returns SQL `NULL`. A stored JSON `null` value returns JSONB `null`.

Read the [installation guide](docs/getting-started.md) before you run the
commands.

## Build the documentation

Use Python 3.11 or newer.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -r requirements-docs.txt
python -m mkdocs serve
```

To build the complete local Pages tree, including the root landing page:

```sh
python scripts/build_pages.py --include-worktree --output _site
```

## Project status

This project is an experiment and a study. Do not use this release in a
production database. Version 0.2.0 supports an extension upgrade from 0.1.0.

The project uses the [PostgreSQL License](LICENSE).
