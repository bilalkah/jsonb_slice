# jsonb_slice

jsonb_slice is a PostgreSQL extension experiment. It asks one question:

> Can PostgreSQL read one field from a large `jsonb` value without reading the
> complete value?

PostgreSQL can move a large value to a TOAST table. It divides the value into
small chunks. The experiment studies which byte ranges an extension needs to
find and return the requested field.

## Current release

`0.5.0` provides key, index, and path accessors. Each accessor can return JSONB
or SQL text. The result rules match PostgreSQL's `->`, `->>`, `#>`, and `#>>`
operators. The release also provides `~>`, `~>>`, `~#>`, and `~#>>` as explicit
entry points to the extension.

This release adds an optional transparent mode. In supported plan expressions,
the mode can route PostgreSQL's six built-in JSONB accessors through the
matching `jsonb_slice` function. The SQL text and PostgreSQL's selected plan do
not change. The mode is off by default.

The partial read path applies to an external, uncompressed JSONB value. A path
read follows object and array containers with TOAST slices and then reads the
selected value. Other storage forms use PostgreSQL's complete-value functions.
This release makes no general performance claim.

Read the [versioned documentation](https://bilalkah.github.io/jsonb_slice/)
or the [0.5.0 release](https://github.com/bilalkah/jsonb_slice/releases/tag/v0.5.0).

## Repository map

| Path | Purpose in this release |
|---|---|
| `.clang-format` | PostgreSQL-style C layout for editors and CI |
| `Makefile` | PostgreSQL extension build and regression entry point |
| `jsonb_slice.control` | Extension identity and default version |
| `src/` | SQL entry points, the partial reader, and transparent-plan support |
| `sql/` | Installation and upgrade scripts for versions 0.1.0 through 0.5.0 |
| `docs/` | Versioned public documentation |
| `test/` | Installation, accessor, storage, planner, and upgrade tests |
| `landing/` | Documentation version index |
| `scripts/` | Versioned documentation build |
| `.github/workflows/` | Package checks, GitHub Releases, and Pages deployment |

## Install 0.5.0

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

The result identifies version `0.5.0`.

Read one top-level object field as JSONB:

```sql
SELECT jsonb_slice_get('{"name": "Ada", "active": true}'::jsonb, 'name');
```

The result is the JSONB string `"Ada"`. A missing field or a non-object input
returns SQL `NULL`. A stored JSON `null` value returns JSONB `null`.

Read one array element as text:

```sql
SELECT jsonb_slice_get_text('["first", "last"]'::jsonb, -1);
```

The result is `last`.

Read a nested path:

```sql
SELECT jsonb_slice_path_text(
    '{"profile": {"name": "Ada"}}'::jsonb,
    ARRAY['profile', 'name']
);
```

The result is `Ada`. The equivalent extension operator is:

```sql
SELECT '{"profile": {"name": "Ada"}}'::jsonb
       ~#>> ARRAY['profile', 'name'];
```

## Use transparent mode

Load the library before PostgreSQL plans the query. Then enable the mode for
the session:

```sql
LOAD 'jsonb_slice';
SET jsonb_slice.transparent = on;

SELECT document ->> 'name'
FROM records;
```

The mode inspects common select-list and filter expressions. It also follows
common child plans, append branches, and subplans. A call outside this scope
keeps PostgreSQL's built-in function. This changes only whether an eligible
call can use the extension; it does not change the result.

For persistent loading, add `jsonb_slice` to `session_preload_libraries` and
start a new session. `CREATE EXTENSION` installs the SQL objects, but it does
not load the planner hook in every database session.

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
production database. Version 0.5.0 supports an extension upgrade from 0.4.0.
PostgreSQL can apply the earlier upgrade scripts in order when an older
installed version must reach 0.5.0.

The project uses the [PostgreSQL License](LICENSE).
