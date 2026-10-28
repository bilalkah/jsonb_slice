# Read selected JSONB data with TOAST slices

<span class="release-mark">0.4.0 · accessors and operators</span>

`jsonb_slice` is a PostgreSQL extension experiment. Version 0.4.0 can read an
object key, an array index, or a nested path. Each read can return JSONB or SQL
text. Its result rules match PostgreSQL's built-in JSONB operators.

The extension uses TOAST slices when the input is external and uncompressed.
It follows the required object and array containers and reads the selected
value. Inline and compressed inputs use PostgreSQL's complete-value functions.

<div class="doc-grid" markdown>

<div class="doc-card" markdown>

## Read forms

Use a text key, an integer array index, or a `text[]` path. Negative indexes
count from the end of an array.

</div>

<div class="doc-card" markdown>

## Result forms

JSONB results preserve JSON `null`. Text results return SQL `NULL` for JSON
`null`. Missing values also return SQL `NULL`.

</div>

</div>

## SQL interface

| Function | Extension operator | PostgreSQL equivalent |
|---|---|---|
| `jsonb_slice_get(jsonb, text)` | `jsonb ~> text` | `jsonb -> text` |
| `jsonb_slice_get(jsonb, integer)` | `jsonb ~> integer` | `jsonb -> integer` |
| `jsonb_slice_get_text(jsonb, text)` | `jsonb ~>> text` | `jsonb ->> text` |
| `jsonb_slice_get_text(jsonb, integer)` | `jsonb ~>> integer` | `jsonb ->> integer` |
| `jsonb_slice_path(jsonb, text[])` | `jsonb ~#> text[]` | `jsonb #> text[]` |
| `jsonb_slice_path_text(jsonb, text[])` | `jsonb ~#>> text[]` | `jsonb #>> text[]` |

## What 0.4.0 contains

- six accessor signatures for key, index, and path reads;
- six operator signatures that call those accessors;
- partial traversal through nested objects and arrays;
- a complete-value fallback for unsupported storage forms;
- fresh-install and 0.3.0 upgrade scripts;
- focused regression tests for functions, operators, storage, and upgrades;
- versioned documentation and release automation.

## Current limits

- The partial path requires an external, uncompressed JSONB value.
- Each visited object lookup reads that object's key bytes.
- The final selected value is read completely.
- The release has no benchmark result or production support promise.

!!! warning "Experimental work"
    Install this version only in a test database. This release makes no
    general performance claim.

[Install 0.4.0](getting-started.md){ .md-button .md-button--primary }
[Read the 0.4.0 notes](releases/0.4.0.md){ .md-button .md-button--primary }
