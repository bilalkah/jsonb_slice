# Read one top-level field

<span class="release-mark">0.2.0 · first JSONB accessor</span>

`jsonb_slice` is a PostgreSQL extension experiment. Version 0.2.0 provides
`jsonb_slice_get(jsonb, text)`. The function reads one top-level object field
and returns the same JSONB value as PostgreSQL's `->` operator with a text key.

This version reads the complete input into memory before it looks for the
field. It does not perform a partial TOAST read.

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

## What 0.2.0 contains

- a PostgreSQL shared library written in C;
- one top-level object accessor;
- fresh-install and 0.1.0 upgrade scripts;
- a regression test for metadata and accessor results;
- versioned documentation and release automation.

## What 0.2.0 does not contain

- partial TOAST reads;
- text, array-index, or path accessors;
- operators or planner hooks;
- benchmark results;
- a production support promise.

!!! warning "Experimental work"
    Install this version only in a test database. It reads the complete JSONB
    value and makes no performance claim.

[Install 0.2.0](getting-started.md){ .md-button .md-button--primary }
[Read the problem](problem.md){ .md-button .md-button--primary }
