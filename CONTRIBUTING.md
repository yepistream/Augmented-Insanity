<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to Augmented Insanity

Contributions are welcome. This document covers what to do before opening a
pull request and what the project expects from the patch itself.

## Before you start

- Read [docs/wiki/Architecture-Overview.md](docs/wiki/Architecture-Overview.md)
  to understand the runtime/module split. Most contributions are either to a
  module (in `samples/`, `module-example/`, or your own external repo) or to
  the runtime infrastructure (in `src/xrt/augins/`, the IPC layer, or the
  Android target).
- Check [docs/wiki/Implementation-Status.md](docs/wiki/Implementation-Status.md)
  and [docs/wiki/Known-Issues.md](docs/wiki/Known-Issues.md). If your
  contribution overlaps with WIP work, open an issue first to coordinate.
- Open an issue for non-trivial changes before doing the work, so the design
  can be discussed.

## Licensing of contributions

By submitting a pull request, you agree that:

- Code you author for `src/xrt/augins/`, `samples/`, `module-example/`,
  `docs/`, `scripts/`, `.github/`, or any other path that is currently
  GPL-3.0-or-later, will be contributed under **GPL-3.0-or-later**.
- Code you author as a modification to a file currently under BSL-1.0, where
  the modification is substantive, may be relicensed by the project under
  GPL-3.0-or-later (BSL-1.0 permits this).
- Code you author as a non-substantive modification to a BSL-1.0 file (typo
  fix, comment cleanup, copyright bump) stays under BSL-1.0; do not flip the
  SPDX header.
- Vendored third-party code retains its upstream license; do not relicense.

If you have any doubt about which case applies, ask in the issue or PR.

## DCO sign-off

Every commit must carry a `Signed-off-by:` line. This is the
[Developer Certificate of Origin](https://developercertificate.org/), a
lightweight statement that you have the right to contribute the code under
the project's license. Configure git once:

```
git config --global user.name "Your Name"
git config --global user.email "you@example.com"
```

Then sign each commit with `-s`:

```
git commit -s -m "Short imperative summary"
```

PRs without DCO sign-offs on every commit will be asked to amend.

## Per-file SPDX header

New files must include a header in the file's natural comment style:

```
// Copyright 2026, Your Name <your@email>
// SPDX-License-Identifier: GPL-3.0-or-later
```

For files at the top of the repository where there is no obvious comment
style (e.g. JSON), use the REUSE convention by adding a sibling
`<filename>.license` file with the same content.

For modifications to existing files, add your copyright line below the
existing ones; do not delete or rewrite upstream copyright statements.

## Code style

The repository carries upstream Monado's `.clang-format` and
`.cmake-format.py` configurations. Run the formatter before committing:

```
./scripts/format-project.sh           # Linux/macOS
# or use clang-format / cmake-format directly on the files you touched
```

For Kotlin in the Android runtime APK, default Android Studio formatting is
acceptable; do not introduce a separate Kotlin formatter unless coordinating
with the project owner.

## Commit messages

- Subject line: 50 characters or fewer, imperative mood ("Add X", not "Added
  X" or "Adds X"). No trailing period.
- Body: wrap at 72 columns. Explain *why* the change is being made; the diff
  shows *what*.
- One logical change per commit. If your PR contains multiple unrelated
  changes, split them into separate commits (or separate PRs).
- Reference issue numbers in the body (`Fixes #42`, `Refs #17`).

## Adding a new module to `samples/`

If you write a module general-purpose enough that it should ship with the
project (rather than as an external `.augins`):

1. Create `samples/augins-<your-module-name>/`.
2. Author `metadata.json`, `settings.json`, the source file, and (typically)
   `CMakeLists.txt` plus `build.gradle`. Use `samples/augins-noop/` as the
   template for a standalone module, or `samples/augins-mercury-handtracking-arcore/`
   for an in-tree-built module.
3. Add the new subproject to `settings.gradle`.
4. Document the module in `docs/wiki/<Your-Module>-Reference.md` (link from
   `docs/wiki/Home.md`).
5. Update `MODIFICATIONS.md` if the module required runtime changes.

## Adding a new IPC hook

If you need the runtime to expose a new IPC dispatch hook (so modules can
intercept a Monado IPC call that isn't currently in the dispatch table):

1. Add the IPC call name + a synthetic xr-name to the `aug_ipc_to_xr`
   dictionary in `src/xrt/ipc/shared/proto.py`. Use a synthetic name like
   `aug_<descriptive>` if the IPC call has no direct OpenXR API analogue.
2. Verify the regenerated `ipc_server_generated.c` has an
   `augins_fire_hooks(...)` call before the relevant `ipc_send`.
3. Document the hook in `docs/wiki/IPC-Hook-Dispatch.md`.
4. Either patch an existing module to use it, or describe the use case in the
   PR.

## Code of conduct

By participating, you agree to abide by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
Report unacceptable behaviour to <kazimirovicmarko@photon.me>.

## Questions

For questions that don't fit a bug-report or feature-request template, open
a GitHub Discussion or email <kazimirovicmarko@photon.me>.
