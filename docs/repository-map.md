# Repository map

0.4.0 is an installable PostgreSQL C extension with key, index, and path
accessors.

| Path | Responsibility |
|---|---|
| `Makefile` | PGXS library, package, and regression commands |
| `jsonb_slice.control` | Extension metadata and default version |
| `src/jsonb_slice.c` | SQL entry points, arguments, fallbacks, and results |
| `src/jsonb_slice_core.c` | TOAST slices and JSONB container parsing |
| `sql/` | Installation and upgrade scripts for versions 0.1.0 through 0.4.0 |
| `test/` | Package, accessor, operator, storage, removal, and upgrade tests |
| `docs/` | Documentation for the current source version |
| `landing/` | The page at `/jsonb_slice/` that lists released versions |
| `scripts/` | Versioned documentation build |
| `.github/workflows/` | Continuous checks, GitHub Releases, and Pages deployment |

## Version ownership

Each source version contains its documentation. The Pages workflow builds the
current version in its versioned path. Release tags keep published versions at
the same address. The release workflow uses the same tag to build the source
archive and the GitHub Release notes.

```text
tag v0.1.0 -> https://bilalkah.github.io/jsonb_slice/0.1.0/
tag v0.2.0 -> https://bilalkah.github.io/jsonb_slice/0.2.0/
tag v0.3.0 -> https://bilalkah.github.io/jsonb_slice/0.3.0/
tag v0.4.0 -> https://bilalkah.github.io/jsonb_slice/0.4.0/
```

The root path is a release index. It is not an alias for one version.
