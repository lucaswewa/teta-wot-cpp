# Releasing

A release is a tag, `vX.Y.Z`, on `main`. Pushing it runs [`.github/workflows/release.yml`](../.github/workflows/release.yml), which tests, builds and publishes. The checklist below comes first: the workflow can't tell whether it was followed. It follows teta-wot's ([`docs/releasing.md`](https://github.com/lucaswewa/teta-wot/blob/main/docs/releasing.md)).

## 1. Choose the version

Read `CHANGELOG.md`'s `Unreleased` section.

- **Anything removed, or a breaking change,** needs a new minor version before 1.0 (`0.1.x` → `0.2.0`). Breaking means breaking the C++ API, a wire profile, configuration files or the command line, TD `id`s, settings files, or the build's requirements (CMake, compilers, vcpkg baseline).
- **Otherwise,** only fixes mean a patch (`0.1.0` → `0.1.1`), and additions a minor version.

A new teta-wot release is adopted deliberately (ADR-0001), with the fixtures re-captured; that is at least a minor version.

## 2. Prepare the branch

1. **In `CHANGELOG.md`,** move the `Unreleased` entries under `## [X.Y.Z] - YYYY-MM-DD` (the tag's date), and leave an empty `## [Unreleased]` above it. Every entry says what changed for users; a breaking change says how to adapt.
2. **Set the version** in `CMakeLists.txt` (`project(... VERSION X.Y.Z)`), `include/teta/wot/version.hpp`, `vcpkg.json` and `ports/teta-wot/vcpkg.json`. Configuring fails if the header and the project disagree, and the release workflow checks the rest.
3. **Merge to `main`**, and wait for CI to pass.

## 3. Soak the release build

The 24-hour soak runs on a release build of what will be tagged ([`tools/README.md`](../tools/README.md)):

```
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release --target soak example-simulated-microscope
build\windows-msvc-release\tools\soak\soak --server build\windows-msvc-release\examples\simulated-microscope\example-simulated-microscope --duration 24h --report soak.csv
build\windows-msvc-release\tools\soak\soak --server build\windows-msvc-release\examples\simulated-microscope\example-simulated-microscope --duration 1h --profile wot
```

Both must print `PASSED`. Keep the output: its summary lines go into the release notes (step 5), and `soak.csv` can be attached to the release.

## 4. Review what is distributed

1. **`THIRD_PARTY_NOTICES.md`:** check it lists everything copied into the repository since the last release (`git log vPREVIOUS..` on `assets/`, `third_party/`, `schema/`, the fixtures and corpora), with checksums up to date.
2. **Gather the binaries' licences, and read the summary** (standard error):

   ```
   python tools/licenses.py build/windows-msvc-release/vcpkg_installed > THIRD_PARTY_LICENSES.txt
   ```

   A licence kind that hasn't been seen before (anything beyond MIT, Apache-2.0, BSD, BSL-1.0, Zlib, CC0 or public domain) needs a decision before it ships.
3. **Try the package's instructions** ([`tools/release/README.txt`](../tools/release/README.txt)) on a Windows computer without a compiler, if the programs changed in ways they describe.

## 5. Tag

```
git switch main && git pull
git tag -a vX.Y.Z -m "teta-wot-cpp X.Y.Z"
git push origin vX.Y.Z
```

The workflow then:

1. checks that the tag matches the project's version (and the vcpkg manifests'), and that the changelog has the version's dated section;
2. builds the release preset and runs every test on it, and the Windows service test;
3. installs the vcpkg overlay port, as users do;
4. packages `simulated-microscope.exe` and `microscope-service.exe` with `microscope.json`, `demo.py`, the README, the changelog, the licence, the notices and the gathered licences;
5. archives the source;
6. writes `SHA256SUMS`;
7. publishes a GitHub release whose notes are the changelog section.

Afterwards, edit the release to add the soak test's summary (and `soak.csv`).

## If something goes wrong

- **The workflow refuses the tag:** delete it (`git push origin :refs/tags/vX.Y.Z` and `git tag -d vX.Y.Z`), fix the version or changelog, and tag again.
- **A published release is broken:** don't move its tag. Fix, and release a patch version.
