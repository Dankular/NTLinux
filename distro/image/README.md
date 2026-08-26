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
- `syslinux/`, `efiboot/`, `grub/`, and the rest of `airootfs/` — copied
  verbatim from archiso's own `releng` reference profile
  (`gitlab.archlinux.org/archlinux/archiso` `configs/releng/`), per Rule 1:
  this is exactly the kind of bootloader/live-boot boilerplate to reuse, not
  hand-author. `airootfs/etc/os-release` is the one file we override for
  NTLinux branding; everything else is releng's as-is.

## Build

```sh
sudo pacman -S --needed archiso
sudo mkarchiso -v -o /path/to/output distro/image/
```

This produces a bootable `.iso` under the output directory.

**Build-verified in-session** (2026-08-26), no Arch host available: ran
`mkarchiso -v` for real inside an `archlinux:latest` container (Docker
daemon started manually, `--network host` + the session's CA bundle to
reach mirrors through the sandbox's TLS-intercepting proxy — see
`docker run` invocation pattern below if reproducing this). It built
`ntlinux-2026.08.26-x86_64.iso` (~2.3GB, 630 resolved packages)
successfully, and a QEMU boot test (software-emulated, no KVM in this
sandbox) confirmed the ISO actually boots: SeaBIOS → ISOLINUX renders the
real menu → kernel+initramfs load → `systemd[1]` starts → live root is
reached → `archiso login:` prompt appears showing `NTLinux (Phase 0)`
branding. No panic, no boot failure — one cosmetic non-fatal systemd
warning (`ModemManager1.service` alias).

```sh
# reproducing the sandboxed build (adjust CA bundle path/proxy per your
# environment's own TLS setup — this incantation is specific to a
# Claude Code sandbox with the agent proxy; a normal Arch host or CI
# runner just needs plain mkarchiso, no proxy dance)
docker run --rm --privileged --network host \
  -v /root/.ccr/ca-bundle.crt:/tmp/ccr-ca.crt:ro \
  -v "$(pwd)/distro/image:/profile:ro" \
  -v /path/to/work:/work -v /path/to/out:/out \
  -e HTTPS_PROXY=http://127.0.0.1:40767 -e https_proxy=http://127.0.0.1:40767 \
  archlinux:latest bash -c '
    cp /tmp/ccr-ca.crt /etc/ca-certificates/trust-source/anchors/ccr.crt
    trust extract-compat
    pacman -Sy --noconfirm && pacman -S --noconfirm --needed archiso
    cp -a /profile/. /build-profile/ && cd /build-profile
    mkarchiso -v -w /work -o /out .
  '
```

**Not yet confirmed** — needs a machine with KVM/GPU passthrough, which this
sandbox doesn't have: logging in and reaching an actual graphical session.
Boot it on real hardware or a KVM-accelerated VM and confirm:

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
- Graphical session (Plasma/Gamescope/Steam/audio) not yet boot-tested —
  see "Not yet confirmed" above.
