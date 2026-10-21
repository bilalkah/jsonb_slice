# Read one field, not the complete document

<span class="release-mark">0.1.0 · research definition</span>

`jsonb_slice` is a PostgreSQL extension experiment. It studies partial reads
of large `jsonb` values. The test case is simple: a query needs one small
field, but the stored document is large.

Version 0.1.0 describes the experiment and installs the extension shell. It
does not provide a JSONB accessor.

<div class="doc-grid" markdown>

<div class="doc-card" markdown>

## Question

Can an extension find one field in an uncompressed, out-of-line `jsonb` value
and fetch only the TOAST chunks that hold the answer?

</div>

<div class="doc-card" markdown>

## Required result

For each supported input, the result must equal the result from PostgreSQL.
For each unsupported input, PostgreSQL must do the work.

</div>

</div>

## What 0.1.0 contains

- a PGXS package that installs `jsonb_slice`;
- a regression test for create, version, empty membership, and drop behavior;
- a PostgreSQL-style C format configuration and CI check;
- an explanation of the research question;
- an explicit statement of the current limits;
- a versioned documentation and Pages build;
- a tagged source archive with a SHA-256 checksum.

## What 0.1.0 does not contain

- C source;
- SQL functions or operators;
- benchmark results;
- a production support promise.

These omissions define the current release. They are not hidden features.

!!! warning "Experimental work"
    Install this version only in a test database. It creates the extension
    identity but does not change JSONB query behavior.

[Install 0.1.0](getting-started.md){ .md-button .md-button--primary }
[Read the problem](problem.md){ .md-button .md-button--primary }
