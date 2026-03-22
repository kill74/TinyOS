# Release Process

TinyOS uses Semantic Versioning (`MAJOR.MINOR.PATCH`).

## Versioning rules

- `MAJOR`: breaking architecture or API changes
- `MINOR`: backward-compatible features
- `PATCH`: backward-compatible fixes and docs corrections

## Release checklist

1. Ensure CI is green on `main`.
2. Update `CHANGELOG.md` from `Unreleased` entries.
3. Create a release commit:
   - `chore(release): vX.Y.Z`
4. Tag and push:
   - `git tag vX.Y.Z`
   - `git push origin main --tags`
5. Create GitHub Release notes from changelog highlights.

## Suggested milestone cadence

- Monthly minor release when active
- Patch releases on bug fixes as needed
