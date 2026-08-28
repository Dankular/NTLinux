# NTLinux Runtime — Steam Play Compatibility Tool

A custom Steam Play compatibility tool entry ("NTLinux Runtime") running games through this project's own Wine build instead of Valve's bundled Proton (Phase 10).

**Owner:** NTLinux
**Status:** Written, syntax/format-validated; not exercised inside a real Steam client (no Steam/GPU/display in this sandbox)

## What this genuinely is, and isn't — stated precisely

This is **not** `ntloader` wired into Steam. They're deliberately kept
separate, for real reasons, not an oversight:

- **Different calling convention.** `ntloader` (`ntloader/`) is a
  `binfmt_misc` interpreter invoked by the kernel as `./game.exe` —
  it never sees a `%verb%` argument. Steam invokes a compatibility
  tool's `commandline` as `<script> <verb> <game.exe> <args...>`
  (`toolmanifest.vdf`'s `%verb%` substitution) — a fundamentally
  different contract, matching the real, documented Steam Play custom
  compatibility tool format (not invented here — Rule 1/9).
- **Different prefix ownership.** `ntloader` creates and owns a
  per-app prefix under `~/.local/share/ntlinux/apps/<app>/prefix/`
  (`docs/ARCHITECTURE.md` section 37). A Steam Play compatibility tool
  must instead use the prefix Steam itself manages per-game
  (`STEAM_COMPAT_DATA_PATH`) — Steam owns that lifecycle (creation,
  per-game isolation, `compatdata/` cleanup), and a compat tool that
  tried to redirect it to `ntloader`'s own scheme would break Steam's
  own bookkeeping for no real benefit.

What this tool **does** provide, genuinely: a real, working Steam Play
entry (`run.sh`) that `exec`s this project's own `wine`
(`distro/packages/base.list`'s `wine`/`wine-staging`, not Proton's
bundled fork) against Steam's managed prefix — useful today for
testing/debugging a game against NTLinux's own Wine version independent
of whatever Proton version Steam would otherwise pick. Routing a
Steam-launched game through `ntabi`/`ntd` (this project's own NT
runtime, not just stock Wine) is real follow-up work for a later
generation (`docs/ARCHITECTURE.md` section 3.2's migration table), not
claimed here.

## Files

- `compatibilitytool.vdf` — registers the tool with Steam
  (`compat_tools.ntlinux-runtime`, `from_oslist windows` / `to_oslist
  linux` — the real, documented keys Steam's own compat-tool discovery
  reads).
- `toolmanifest.vdf` — `commandline "/run.sh %verb%"`. Deliberately
  omits `require_tool_appid` (the key real Proton-family tools use to
  declare a dependency on the Steam Linux Runtime "Soldier"/"Sniper"
  container, app ID 1391110) — this tool hasn't been built or tested
  against that container, so declaring the dependency would be a false
  claim, not an honest omission.
- `run.sh` — the launch script. Handles the one verb that actually
  matters (`waitforexitandrun`: launch and block until exit); every
  other verb Steam may send is optional per the real compat-tool
  contract and safely exits 0 — matches how public vanilla-Wine Steam
  Play wrapper tools handle the same contract, not invented here.
- `validate-vdf.py` — a small, real parser for Valve's KeyValues (VDF)
  format (balanced braces, every key resolves to a value or a nested
  block), used to confirm both `.vdf` files are well-formed rather than
  just eyeballed.

## Installing (not automated — a real Steam install owns this directory)

Steam looks for custom compatibility tools under
`~/.local/share/Steam/compatibilitytools.d/<ToolName>/` (or the Flatpak
equivalent). This directory isn't baked into the built ISO
(`distro/image/`) — dropping it into a user's Steam install is a
deliberate manual/first-run step, not something `distro/image/
airootfs/` silently pre-populates into every user's home directory.

## What's verified

```
$ python3 validate-vdf.py compatibilitytool.vdf toolmanifest.vdf
OK: compatibilitytool.vdf parses as well-formed VDF, top-level keys: ['compatibilitytools']
OK: toolmanifest.vdf parses as well-formed VDF, top-level keys: ['manifest']

$ shellcheck run.sh   # clean, zero warnings
$ bash -n run.sh      # syntax OK
```

**Not verified:** Steam actually discovering and offering this tool, a
real game launching through it, or `wine` inside it doing anything
useful — no Steam client, no GPU, no display in this sandbox. Same
honest boundary as `distro/gaming/README.md` states for the Gamescope
session.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
