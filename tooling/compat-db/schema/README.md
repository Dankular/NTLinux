# Compat-DB Schema

Phase 10's "plus the driver compatibility database" deliverable — a real JSON Schema for tracked app/game/driver compatibility entries.

**Owner:** NTLinux
**Status:** Schema written, validated against two genuinely true seed entries and a deliberately-invalid negative test (Phase 10)

`tooling/compat-db/README.md` names what this database needs to track:
version, Windows target, required overrides, known bugs, test status.
`compat-entry.schema.json` formalizes that as a real JSON Schema
(draft 2020-12) rather than leaving it as prose. One deliberate design
choice worth stating: `kind` (`application`/`game`/`driver`) shares a
single `test_status` enum across all three
(`untested`/`builds`/`loads`/`boots`/`playable`/`verified`/`perfect`)
rather than a per-kind enum — `builds`/`loads`/`verified` read
naturally for a driver entry, `boots`/`playable`/`perfect` for a game,
and duplicating the enum per kind would cost more than it buys (Rule 2).

## Real seed data, not fabricated examples

`seed-entries.json` has two entries, both pulled from this project's
own already-verified history rather than invented for illustration:

- `wine-notepad-ntloader` — Phase 1's real result: Wine's bundled
  `notepad.exe` launched directly via `ntloader`'s `binfmt_misc`
  registration, a real window appearing on a virtual display (see
  `ntloader/README.md`).
- `ntbridge-vsdev` — Phase 5's real result: `driver/vsdev/vsdev.c`
  loaded and performed genuine I/O on a real ReactOS x64 kernel under
  QEMU, reproduced across two full runs (see `driver/vsdev/README.md`).

## Verified

```
$ python3 validate.py
OK: seed-entries.json: wine-notepad-ntloader
OK: seed-entries.json: ntbridge-vsdev
```

Negative test (confirms the validator actually rejects bad data, not
just always returning OK — run during development, not shipped as a
fixture):

```
$ echo '[{"id":"Bad ID!","name":"x"}]' > /tmp/bad.json
$ python3 validate.py /tmp/bad.json
FAIL: /tmp/bad.json: Bad ID!: 'kind' is a required property
```

## Known gap

The database proper — an actual growing collection of entries for real
apps/games/drivers, and whatever query/report tooling makes it useful
day to day — is still unstarted, same as `tooling/compat-db/README.md`
already states. This schema plus two true seed entries is the
structural starting point Phase 10 asks for, not a populated database.

See [`docs/ARCHITECTURE.md`](/docs/ARCHITECTURE.md) for full architectural context and [`ROADMAP.md`](/ROADMAP.md) for phase sequencing. Before implementing anything here, check whether the capability already exists upstream (Wine / Proton / ReactOS / Linux / Mesa / DXVK / vkd3d-proton / Gamescope / PipeWire / VFIO-IOMMU-KVM) per Rule 1 in `CLAUDE.md`.
