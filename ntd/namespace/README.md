# Object Namespace

NT object namespace: \Device, \Sessions, \BaseNamedObjects, \KnownDlls, \RPC Control, ??\C:, ??\D:, UNC. Do not collapse to Unix path strings early (ARCHITECTURE.md section 6).

**Owner:** NTLinux
**Status:** Not started (real namespace); a flat placeholder exists in `ntd/ntd.c`

`ntd.c`'s Phase 2 object manager uses a flat name→object map (any name is
just a string key, no `\Device`/`\Sessions`/`\BaseNamedObjects` hierarchy,
no `??\C:` drive-letter namespace) — a deliberate, documented Gen2
simplification, not this real namespace. Building the actual hierarchy
described above is still fully unstarted.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.

> **Host-agnostic:** this component is part of the NT Runtime — it must build/run on any reasonably modern Linux distro, not just the NTLinux reference image in `distro/`. See ADR-0001 in `docs/DECISIONS.md`.
