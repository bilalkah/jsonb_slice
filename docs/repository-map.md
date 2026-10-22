# Repository map

0.1.0 is an installable SQL-only extension shell.

| Path | Responsibility |
|---|---|
| `Makefile` | PGXS package and regression commands |
| `jsonb_slice.control` | Extension metadata and default version |
| `sql/` | Versioned extension installation script |
| `test/` | Package regression test |
| `docs/` | Documentation for the current source version |
| `landing/` | The page at `/jsonb_slice/` that lists all versions |
| `scripts/` | Versioned documentation build |
| `.github/workflows/` | Continuous checks, release publishing, and Pages deployment |

## Version ownership

Each source version contains its documentation. The Pages workflow builds the
current version in its versioned path. Release tags keep published versions at
the same address. The release workflow uses the same tag to build
the source archive and the GitHub Release notes.

For example:

```text
tag v0.1.0
  -> https://bilalkah.github.io/jsonb_slice/0.1.0/
```

The root path is a release index. It is not an alias for one version. The
address above identifies the 0.1.0 documentation.
