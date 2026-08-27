# App Installer Tooling

Windows application installer / per-app environment management tooling (ARCHITECTURE.md section 37, Phase 1).

**Owner:** NTLinux
**Status:** Implemented and verified

## `ntlinux-app`

A dependency-light Python 3 CLI (stdlib only, plus shelling out to
`icoutils` — Rule 1, not a hand-rolled ICO/PE-resource parser) with three
subcommands:

- `ntlinux-app desktop <exe> [--name NAME]` — for a portable `.exe` that
  needs no installer: creates its per-app environment, extracts a real
  desktop icon from the PE resources, and writes a `~/.local/share/applications/*.desktop`
  entry whose `Exec=` is just the `.exe`'s own path — `binfmt_misc` +
  `ntloader` make that directly runnable, so no wrapper is needed.
- `ntlinux-app run-installer <setup.exe> [--target ...]` — runs a Windows
  installer inside a fresh prefix, diffs `Program Files*` before/after to
  find what it installed, and generates the desktop entry for that.
- `ntlinux-app list` — lists installed apps and their per-app directories.

**App-id scheme is intentionally duplicated** between this tool (Python)
and `ntloader/ntloader.c` (C): both compute `basename(exe) +
"-" + hex(fnv1a64(realpath(exe)))`, so running the same `.exe` directly
(via `binfmt_misc`) and launching it via a generated `.desktop` file
resolve to the *same* per-app environment. This is a Gen1-scope
duplication, documented in both places — a real shared implementation
belongs in `libntabi` once Phase 2 exists, not two independently
maintained copies of the same hash function forever.

## Verified

Tested end-to-end against Wine's real `notepad.exe`: `ntlinux-app desktop`
computed the identical app-id `ntloader` had already created for the same
file, extracted a real 256×256 icon from the binary's own resources via
`wrestool`/`icotool`, and wrote a working `.desktop` entry. See
`ROADMAP.md` Phase 1 for the full account.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
