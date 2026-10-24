# Read one field with TOAST slices

<span class="release-mark">0.3.0 · partial read path</span>

`jsonb_slice` is a PostgreSQL extension experiment. Version 0.3.0 provides
`jsonb_slice_get(jsonb, text)`. The function reads one top-level object field
and returns the same JSONB value as PostgreSQL's `->` operator with a text key.

The function uses TOAST slices when the input is external and uncompressed.
It reads the root directory, the object keys, and the selected value. The
function uses PostgreSQL's normal complete-value path for other storage forms.

<div class="doc-grid" markdown>

<div class="doc-card" markdown>

## Input

The first argument is a JSONB value. The second argument is a top-level object
field name.

</div>

<div class="doc-card" markdown>

## Result

The result matches `document -> field_name`. This rule includes missing fields,
JSON null, containers, arrays, scalars, and SQL null arguments.

</div>

</div>

## What 0.3.0 contains

- a PostgreSQL shared library written in C;
- one top-level object accessor;
- a partial read path for external, uncompressed input;
- a complete-value fallback for other input;
- fresh-install and 0.2.0 upgrade scripts;
- regression tests for result rules and storage paths;
- versioned documentation and release automation.

## Current limits

- The function accepts a top-level text key only.
- The partial path requires an external, uncompressed JSONB value.
- The function can read the complete selected field value.
- The release has no benchmark result or production support promise.

!!! warning "Experimental work"
    Install this version only in a test database. This release makes no
    performance claim.

[Install 0.3.0](getting-started.md){ .md-button .md-button--primary }
[Read the problem](problem.md){ .md-button .md-button--primary }
