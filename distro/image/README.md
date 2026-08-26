# distro/image — NTLinux reference distro image

An [`archiso`](https://gitlab.archlinux.org/archlinux/archiso) profile that
builds the Phase 0 NTLinux live/install ISO: Arch Linux base, Wayland
(Plasma), PipeWire, Mesa/Vulkan, Wine/Wine-Staging, Steam, Gamescope.

This is the **reference distro**, not the NT Runtime itself — see
`docs/DECISIONS.md` ADR-0001. Nothing under `ntloader/`, `ntabi/`, `ntd/`,
or `runtime/` may depend on anything specific to this image.

## Files

- `profiledef.sh` — archiso profile definition (ISO name/label, boot modes,
  compression).
- `pacman.conf` — repo config for the build (adds `multilib`).
- `packages.x86_64` — the package list. See `distro/packages/base.list` for
  the same list with per-package rationale tied back to
  `docs/ARCHITECTURE.md`.
- `airootfs/` — files overlaid onto the live filesystem verbatim (currently
  just an `os-release` branding override).

## Build (on an Arch host — not run in this sandbox)

This profile has been written and reviewed but **not build-verified**: this
development container has no pacman/archiso toolchain or Arch mirror
access. Build and boot-test it on an actual Arch Linux host or CI runner
before treating Phase 0 as done (tracked in `ROADMAP.md`).

```sh
sudo pacman -S --needed archiso
sudo mkarchiso -v -o /path/to/output distro/image/
```

This produces a bootable `.iso` under the output directory. Boot it (real
hardware or a VM with virtualized GPU/KVM passthrough for a realistic
Steam/Wine test) and confirm:

- Plasma (Wayland) session starts.
- `pipewire`/`wireplumber` are running and produce audio.
- `steam` launches and can install/run a test game through Proton.
- `glxinfo`/`vulkaninfo` show a working Mesa/Vulkan driver.

## Known gaps (see `ROADMAP.md` Phase 0 checklist)

- `CONFIG_NTSYNC` is assumed enabled in Arch's `linux` package (mainline
  since 6.14) but hasn't been confirmed on a built image yet — see
  `kernel/config/`.
- `dxvk-bin`/`vkd3d-proton` AUR packages aren't pre-baked (see the note in
  `packages.x86_64`); Proton's own bundled copies cover Steam games in the
  meantime.
- No installer yet (`distro/installer/` — live-boot only for now).
