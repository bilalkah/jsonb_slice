# Repository map

0.2.0 is an installable PostgreSQL C extension with one JSONB accessor.

| Path | Responsibility |
|---|---|
| `Makefile` | PGXS library, package, and regression commands |
| `jsonb_slice.control` | Extension metadata and default version |
| `src/` | C source for the top-level object lookup |
| `sql/` | 0.1.0 and 0.2.0 installation and upgrade scripts |
| `test/` | Package, accessor, removal, and upgrade regression test |
| `docs/` | Documentation for the current source version |
| `landing/` | The page at `/jsonb_slice/` that lists released versions |
| `scripts/` | Versioned documentation build |
| `.github/workflows/` | Continuous checks, release publishing, and Pages deployment |

## Version ownership

Each source version contains its documentation. The Pages workflow builds the
current version in its versioned path. Release tags keep published versions at
the same address. The release workflow uses the same tag to build the source
archive and the GitHub Release notes.

```text
tag v0.1.0 -> https://bilalkah.github.io/jsonb_slice/0.1.0/
tag v0.2.0 -> https://bilalkah.github.io/jsonb_slice/0.2.0/
```

The root path is a release index. It is not an alias for one version.
