# ntloader

PE executable launcher. Registered via a Linux binary-format dispatch mechanism so `./game.exe` runs directly, without invoking `wine` explicitly (ARCHITECTURE.md section 4).

**Owner:** NTLinux (dispatch/environment) + Wine (actual PE loading — see below)
**Status:** Implemented and verified (Gen1 — see `docs/ARCHITECTURE.md` §3.2)

## What this is

A small, dependency-free C program (`ntloader.c`, ~350 lines) registered as
a Linux `binfmt_misc` interpreter for the `MZ` (DOS/PE) magic bytes. It:

1. Reads just enough of the PE header (DOS stub → `e_lfanew` → `PE\0\0` →
   `IMAGE_FILE_HEADER.Machine`) to confirm the file is really PE and pick
   an architecture — not a full PE loader.
2. Resolves/creates a per-application NT environment under
   `~/.local/share/ntlinux/apps/<basename>-<hash>/` (`docs/ARCHITECTURE.md`
   §37: `drive-c/`, `registry/`, `compat/`, `dll-overrides/`, `state/`,
   plus a `prefix/` holding the real Wine `WINEPREFIX`).
3. `execve()`s into `wine` with `WINEPREFIX` and (optionally)
   `WINEDLLOVERRIDES` set, and the original argv passed through.

Actual PE loading — section mapping, imports, relocations, TLS, PEB/TEB
construction, ntdll — is 100% Wine's, unmodified. This is deliberate: it's
Generation 1 in `docs/ARCHITECTURE.md` §3.2's migration table ("NTLinux
loader" dispatching to standard Wine), not a from-scratch PE loader. Later
generations may route more of this through `ntabi`/`ntd`; ntloader's job
stays "route correctly and set up the environment," not "reimplement what
Wine already does" (Rule 1).

## Verified

Built and tested end-to-end in-session against Wine's own real
`notepad.exe` PE binary — registered with the kernel's live
`binfmt_misc`, then ran `./notepad.exe` directly (no `wine`, no
`ntloader` in the command line) with a virtual display attached, and a
real "Untitled - Notepad" window (721×512, Wine's actual icon extracted
from the PE resources) appeared. Compiles clean with `-Wall -Wextra`, zero
warnings. See `ROADMAP.md` Phase 1 for the full account, including the
one intentional divergence from a literal reading of §4 (delegating real
PE loading to Wine rather than reimplementing PEB/TEB construction).

**Not yet baked into the built ISO** — the compile step is wired into
`distro/image/build.sh` + `airootfs/root/customize_airootfs.sh`, but that
hasn't been re-verified with a full image rebuild in this session (Phase 0
rebuild cycles are expensive — see `distro/image/README.md`). The
mechanism itself (kernel binfmt_misc → this exact binary → Wine) is
verified for real, just not yet re-confirmed on a freshly booted image.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
