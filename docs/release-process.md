# Release process

Short, opinionated, reversible.

## Versioning

[Semantic Versioning 2.0](https://semver.org). Version lives in two places, kept in sync:

- `VERSION` (the one tooling reads at runtime)
- `CMakeLists.txt`'s `project(VERSION ...)` (the one CMake reads)

Release-bumping rule of thumb:

| Change | Bump |
|---|---|
| Wire-protocol break, schema rename, removed CLI flag | major |
| New feature (LLM backend lands, new translator), additive protocol field | minor |
| Bug fix, performance improvement, doc | patch |

## Steps for v0.X.Y

1. **All green local + CI**: `ctest`, `shellcheck`, manual L3 walkthrough.
2. **Update CHANGELOG.md**: move "Unreleased" → "v0.X.Y — YYYY-MM-DD".
3. **Bump VERSION + CMakeLists**: same number, same commit.
4. **Commit**: `chore: release v0.X.Y`.
5. **Tag**: `git tag -a v0.X.Y -m "release v0.X.Y"`.
6. **Push**: `git push origin main && git push origin v0.X.Y`.
7. **GitHub release**: `gh release create v0.X.Y --notes-file -` with the
   CHANGELOG section as body. CI builds and a `.tar.gz` source archive
   gets attached automatically; pre-built macOS binaries are
   Phase 3.2+ work.

## Pre-release (alpha/beta)

`v0.X.Y-alpha.N` and `v0.X.Y-beta.N`. CHANGELOG entry lives under "Unreleased" until promoted.

## Hotfix

For a critical fix on top of a released version that doesn't ship from main yet:

1. Branch off the tag: `git checkout -b hotfix/v0.X.Y v0.X.Y`.
2. Cherry-pick or write the fix.
3. Bump patch, update CHANGELOG, tag `v0.X.Y+1`, push.
4. Merge `hotfix/...` back to `main`.

## What we do NOT do

- Skip CI on release tags.
- Force-push tags.
- Sign releases via personal key without `gpg.signingkey` configured at the project level.
- Auto-publish to anywhere (Homebrew tap, Mac App Store) — those are
  conscious release steps that are Phase 3.2+ when we have stable
  download artifacts.
