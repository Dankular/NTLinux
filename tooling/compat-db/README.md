# Compatibility Database

Central database of application/game/driver compatibility state: version, Windows target, required overrides, known bugs, test status (ARCHITECTURE.md section 46).

**Owner:** NTLinux
**Status:** Not started (the app/game/driver database itself); its foundational input tools are built — see `ntexports/` and `ntprobe/`

Before this database can track "is `NtCreateFile` implemented," it needs
the actual, complete list of what real Windows exports in the first
place — sourced from a real machine, not documentation or memory.
`ntexports/` is that: a Windows-side PE export walker plus a gap analyzer
against Wine's/ReactOS's own `.spec` files (static: "does the symbol
exist"). `ntprobe/` goes one level deeper: a Windows-side tool that makes
real `Nt*`/`Zw*` calls and records their real behavior (dynamic: "what
does calling it actually do"), giving `ntd`/`ntabi`'s own reimplemented
NT object semantics a genuine behavioral reference to diff against
instead of only self-consistency. Both built and verified (mechanism
only — see each README for the real-Windows-data gap); the compatibility
database proper (per-app/game/driver tracking) is still unstarted.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
