# Gaming Integration

Steam/Proton/Gamescope integration pieces that go beyond `distro/packages/base.list`'s baseline package selection (ARCHITECTURE.md section 10/38, Phase 10).

**Owner:** NTLinux (integration glue) + Valve (Steam, Proton, Gamescope) + Wine (this project's own Wine build)
**Status:** Session/compat-tool integration artifacts written and validated at the syntax/schema level (Phase 10); no live Steam/GPU verification possible in this sandbox

`distro/packages/base.list` already lists the upstream packages
(`steam`, `wine`, `wine-staging`, `gamescope`, `gamemode`, ...) — that's
Phase 0's baseline, already done. What Phase 10 adds is the glue between
them:

- `compat-tool/` — a real Steam Play custom compatibility tool
  (`compatibilitytool.vdf`/`toolmanifest.vdf`/`run.sh`) that runs a game
  through this project's own Wine build instead of Valve's bundled
  Proton — see that directory's README for exactly what it does and
  does not integrate with (notably: not `ntloader` — different calling
  convention and prefix-ownership model, explained there).
- `distro/image/airootfs/usr/share/wayland-sessions/
  gamescope-session.desktop` + `.../usr/bin/gamescope-session` — the
  default "Game Mode" session (CLAUDE.md's "Open decisions": Gamescope
  primary, Plasma optional desktop-mode) a display manager offers at
  login, real syntax matching the documented SteamOS/HoloISO/ChimeraOS
  convention for this exact file/script pair, not invented here.
- `tooling/compat-db/schema/` — Phase 10's other explicit ask ("plus
  the driver compatibility database"): a real JSON Schema for tracked
  app/game/driver compatibility entries, with two genuinely true seed
  entries pulled from this project's own already-verified history
  (Phase 1's ntloader+Wine notepad.exe run, Phase 5's vsdev.c driver
  test) rather than fabricated examples.

## What's verified, and how — this sandbox has no GPU, no display, no KVM

Real, run checks, at the level this sandbox actually allows:

- `gamescope-session.desktop`: run through `desktop-file-validate`
  (real tool, installed this session). One real, understood warning:
  `DesktopNames` isn't part of the strict Desktop Entry Specification
  `desktop-file-validate` checks against (that spec targets application
  launchers), but it *is* the real, standard key display managers
  (SDDM/GDM/LightDM) actually parse for session files under
  `wayland-sessions/`/`xsessions/` — a known, expected class of warning
  for legitimate session `.desktop` files, not a bug in this one.
- `gamescope-session` and `compat-tool/run.sh`: `bash -n` (syntax) and
  `shellcheck` (real static analysis, installed this session) — both
  clean, zero warnings.
- `compat-tool/*.vdf`: `compat-tool/validate-vdf.py`, a real (if
  minimal) parser for Valve's KeyValues format written for this check
  — confirms both files are well-formed VDF (balanced braces, every key
  has a value or a nested block), not just eyeballed.
- `tooling/compat-db/schema/`: `validate.py` runs the real `jsonschema`
  package against both seed entries (pass) and, spot-checked, against a
  deliberately invalid entry (correctly rejected — `'kind' is a
  required property` — confirming the validator actually validates,
  not just always returns OK).

**What is *not* verified, stated precisely, same standard as every
other phase in this repo:** nothing here has been exercised inside a
real Steam client, a real Gamescope compositor, or against real GPU/
display hardware — this sandbox has none of those (no `/dev/dri`, no
KVM, no display server), the same category of architectural absence
Phase 6 (VFIO) and Phase 8 (USB) hit. A real game launching under this
session, actually rendering through DXVK/vkd3d-proton, and a controller
being recognized are all genuine follow-up verification, needing real
hardware or a substantially different sandbox, not attempted here.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
