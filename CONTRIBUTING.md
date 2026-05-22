<!--
Copyright 2026, Marko Kazimirovic <kazimirovicmarko@photon.me>
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Contributing to Augmented Insanity

Procedure for opening a pull request and the expectations on the
patch itself.

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

By submitting a pull request, the contributor agrees that:

- Code authored for `src/xrt/augins/`, `samples/`,
  `module-example/`, `docs/`, `scripts/`, `.github/`, or any
  other path currently under GPL-3.0-or-later is contributed
  under GPL-3.0-or-later.
- Substantive modifications to a file currently under BSL-1.0
  may be relicensed by the project under GPL-3.0-or-later
  (BSL-1.0 permits this).
- Non-substantive modifications to a BSL-1.0 file (typo fix,
  comment cleanup, copyright bump) stay under BSL-1.0; do not
  flip the SPDX header.
- Vendored third-party code retains its upstream license; do
  not relicense.

Ask in the issue or PR for unclear cases.

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

For a module general-purpose enough to ship with the project
(rather than as an external `.augins`):

1. Create `samples/augins-<your-module-name>/`.
2. Author `metadata.json`, the source file(s), `CMakeLists.txt`,
   and `build.gradle`. Use `samples/augins-test-noop/` as the
   template for a smallest-possible module, or
   `samples/augins-arcore-headpose/` for a production module
   with vendored sibling `.so` files.
3. Add the new subproject to `settings.gradle`.
4. Document the module in `docs/wiki/<Your-Module>-Reference.md`
   and link it from `docs/wiki/Home.md` and
   `docs/wiki/Implementation-Status.md`.
5. Update `MODIFICATIONS.md` if the module required runtime
   changes.

See [docs/wiki/Building-A-Module.md](docs/wiki/Building-A-Module.md)
for the per-module build setup.

## Adding a new dispatchable IPC call

To make a Monado IPC call routable through modules:

1. Pick the module-facing name. For an IPC call with a 1:1
   OpenXR equivalent, use the OpenXR name. Otherwise invent a
   synthetic `aug_<CamelCase>` name and document the choice in
   the `proto.py` comment.
2. Add the entry to `aug_ipc_to_xr` in
   `src/xrt/ipc/shared/proto.py` (IPC call -> module-facing
   name). Add the module-facing name to
   `aug_implemented_adapters` in the same file.
3. Declare and implement `aug_adapter_<call>` in
   `src/xrt/augins/adapters.{h,cpp}`. Same signature as
   `ipc_handle_<call>` in
   `src/xrt/ipc/server/ipc_server_handler.c`. Follow the
   five-step template documented in
   [docs/wiki/Service-Side-Dispatch.md](docs/wiki/Service-Side-Dispatch.md).
4. Either update an existing module to consume the new name in
   its `Implemented_Functions`, or describe the use case in the
   PR.

## Code of conduct

By participating, you agree to abide by [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md).
Report unacceptable behaviour to <kazimirovicmarko@photon.me>.

## Questions

For questions that don't fit a bug-report or feature-request template, open
a GitHub Discussion or email <kazimirovicmarko@photon.me>.
