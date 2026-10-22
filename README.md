# jsonb_slice

jsonb_slice is a PostgreSQL extension experiment. It asks one question:

> Can PostgreSQL read one field from a large `jsonb` value without reading the
> complete value?

PostgreSQL can move a large value to a TOAST table. It divides the value into
small chunks. The experiment studies when an extension can read only the
chunks that contain the requested field.

## Current release

`0.1.0` defines the problem and the test boundary. It provides an installable
extension shell. `CREATE EXTENSION jsonb_slice` works, but this version has no
JSONB accessors.

The 0.1.0 checks cover the package identity, version, installation, and
documentation build.

Read the [versioned documentation](https://bilalkah.github.io/jsonb_slice/)
or the [0.1.0 release notes](docs/releases/0.1.0.md).

## Repository map

| Path | Purpose in this release |
|---|---|
| `Makefile` | PostgreSQL extension build and regression entry point |
| `jsonb_slice.control` | Extension identity and default version |
| `sql/` | Versioned 0.1.0 installation script |
| `docs/` | Versioned public documentation |
| `test/` | 0.1.0 package regression test |
| `landing/` | Documentation version index |
| `scripts/` | Versioned documentation build |
| `.github/workflows/` | Documentation checks and GitHub Pages deployment |

## Install 0.1.0

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

The result identifies version `0.1.0`. This version does not create a
function, operator, background worker, or planner hook.

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
production database. Version 0.1.0 has no supported upgrade path.

The project uses the [PostgreSQL License](LICENSE).
