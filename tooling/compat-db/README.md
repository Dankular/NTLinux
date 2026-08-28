# Compatibility Database

Central database of application/game/driver compatibility state: version, Windows target, required overrides, known bugs, test status (ARCHITECTURE.md section 46).

**Owner:** NTLinux
**Status:** Not started (the app/game/driver database itself); its foundational input tools are built — see `ntexports/`, `ntprobe/`, and `ddkgap/`

Before this database can track "is `NtCreateFile` implemented," it needs
the actual, complete list of what real Windows exports in the first
place — sourced from a real machine, not documentation or memory.
`ntexports/` is that: a Windows-side PE export walker plus a gap analyzer
against Wine's/ReactOS's own `.spec` files (static: "does the symbol
exist"). `ntprobe/` goes one level deeper: a Windows-side tool that makes
real `Nt*`/`Zw*` calls and records their real behavior (dynamic: "what
does calling it actually do"), giving `ntd`/`ntabi`'s own reimplemented
NT object semantics a genuine behavioral reference to diff against
instead of only self-consistency. `ddkgap/` (Phase 9) is the kernel-mode
counterpart to `ntexports/`: instead of a real Windows machine's DLL
exports (unavailable in this sandbox), it probes what this project's
own DDK toolchain actually supports today for KMDF/modern WDM/NDIS 6.x/
PnP/power/memory-management/security — the concrete prerequisite for
any of Phase 9's upstream driver-compatibility work. All three built and
verified (mechanism only for `ntexports`/`ntprobe` — see each README
for the real-Windows-data gap; `ddkgap` measures this toolchain
directly, so its results are real without needing external data); the
compatibility database proper (per-app/game/driver tracking) is still
unstarted.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
