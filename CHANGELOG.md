# Changelog

All notable changes to hotc233-unity are tracked here.

## [1.0.0] - 2026-06-13

### Added

- Added one-command project-root installers for Windows cmd, PowerShell, macOS, Linux, and Git Bash.
- Added GitHub Actions CI for package metadata, installer script syntax, whitespace, and internal native naming guards.
- Added tag-based release CI that validates `package.json`, builds `hotc233-unity-<version>.zip`, writes SHA-256 checksums, uploads artifacts, and creates GitHub Releases from `v<package.json version>` tags.
- Added a GitHub Pages documentation site under `docs/`, including ecosystem and platform support pages.
- Added documentation site validation and automatic Pages deployment through `.github/workflows/docs.yml`.

### Changed

- Renamed the package path to `Assets/neko233/hotc233-unity`.
- Migrated internal native runtime paths, generated C++ paths, macros, templates, and editor references from historical `hybridclr` naming to `hotc233` naming.
- Kept the package as a single Git-maintained repository intended for direct clone or submodule use.
- Documented the public docs URL, ecosystem roadmap, and platform support verification policy.

### Known Gaps

- Community ecosystem maturity, third-party benchmark parity, commercial DHE equivalence, and long-duration true-device stability still need continued work before hotc233-unity can claim HybridCLR-level maturity.
