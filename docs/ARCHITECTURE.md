# NTLinux — Codex

## Project intent

NTLinux is a Linux distribution designed to make Windows applications and Windows driver binaries first-class workloads rather than treating them as foreign programs launched through a compatibility command.

The core idea is **not** to rewrite Windows from scratch.

The project should deliberately reuse the strongest existing implementations:

- **Linux** for the host kernel, hardware support, scheduling, filesystems, networking, security boundaries, DRM/Vulkan, input, audio, virtualization, VFIO and IOMMU.
- **Wine** for the mature Win32/Win64 user-mode API implementation.
- **Proton** for Windows gaming compatibility, Steam integration, DXVK, vkd3d-proton, game fixes and runtime packaging.
- **ReactOS** for NT kernel semantics, WDM, the I/O Manager, Object Manager, PnP, IRPs, device stacks, registry/kernel services and Windows-driver execution.
- **KVM/VFIO/IOMMUFD** for safely hosting Windows kernel drivers that genuinely require ring 0 or direct hardware access.

The unique part of NTLinux is the **integration layer between these systems**.

The target architecture is:

```text
                    NTLinux

        Linux applications      Windows applications
                │                       │
                │                Win32 / Win64
                │                       │
                │                 Wine-derived DLLs
                │                       │
                │                     ntdll
                │                       │
                └───────────┬───────────┘
                            │
                      NT User ABI
                            │
                     Linux host kernel
                            │
            ┌───────────────┴────────────────┐
            │                                │
     native Linux drivers              NT Driver Bridge
                                             │
                                     shared-memory IPC
                                             │
                                            KVM
                                             │
                                  ReactOS NT Driver Cell
                                             │
                           ┌─────────────────┼─────────────────┐
                           │                 │                 │
                       ntoskrnl           PnP/WDM          KMDF/NDIS
                           │                 │                 │
                           └─────────────────┼─────────────────┘
                                             │
                                      Windows .sys
                                             │
                                          VFIO
                                             │
                                         hardware
```

---

# 1. Core principles

## 1.1 Do not rewrite solved compatibility layers

Before implementing any Windows subsystem, check whether the capability already exists in:

1. Wine
2. Proton
3. ReactOS
4. Linux kernel
5. Mesa
6. DXVK
7. vkd3d-proton
8. Gamescope
9. PipeWire
10. VFIO / IOMMUFD / KVM

The default decision should be to **adapt, expose, refactor or upstream existing code**, not duplicate it.

---

## 1.2 Linux remains the real host operating system

Linux owns:

- CPU scheduling
- physical memory
- Linux processes
- Linux device drivers
- DRM/KMS
- Vulkan
- filesystems
- networking
- USB
- PCI enumeration
- Bluetooth
- audio devices
- input devices
- power management
- virtualization
- security isolation
- namespaces/cgroups
- IOMMU configuration

NTLinux must not attempt to replace Linux with another kernel.

---

## 1.3 NT becomes an operating-system personality

Windows software should see NT semantics even though Linux remains underneath.

Conceptually:

```text
Win32
  │
kernelbase/kernel32
  │
ntdll
  │
NT native calls
  │
NTLinux ABI
  │
Linux
```

Examples include:

```text
NtCreateFile
NtOpenFile
NtReadFile
NtWriteFile
NtCreateSection
NtMapViewOfSection
NtAllocateVirtualMemory
NtProtectVirtualMemory
NtCreateEvent
NtCreateMutant
NtCreateSemaphore
NtWaitForSingleObject
NtWaitForMultipleObjects
NtCreateThreadEx
NtQueryInformationProcess
NtOpenKey
NtCreateKey
NtQueryValueKey
NtDeviceIoControlFile
```

The first implementation may still delegate many operations to Wine/wineserver.

The long-term goal is to progressively move hot and semantically important NT operations into NTLinux-native facilities.

---

# 2. Major architectural pillars

NTLinux has four primary pillars.

```text
1. NT User Runtime
2. NT Host ABI
3. ReactOS NT Driver Cell
4. Proton Gaming Runtime
```

---

# 3. NT user runtime

## 3.1 Start from Wine

Do not create a fresh implementation of:

```text
kernel32.dll
kernelbase.dll
user32.dll
gdi32.dll
advapi32.dll
ole32.dll
combase.dll
shell32.dll
ws2_32.dll
winmm.dll
...
```

Wine already contains mature implementations.

NTLinux should initially fork or package Wine and alter the lower boundary.

Target:

```text
Windows EXE
    │
Wine Win32 DLLs
    │
ntdll
    │
NTLinux ABI
```

instead of:

```text
Windows EXE
    │
Wine Win32 DLLs
    │
ntdll
    │
Wine Unix translation
    │
wineserver
    │
Linux
```

---

## 3.2 Progressive wineserver reduction

Do not require removal of wineserver immediately.

Use migration stages:

```text
Generation 0    standard Wine
Generation 1    NTLinux loader
Generation 2    selected NT calls bypass wineserver
Generation 3    NT objects/waits/sections handled natively
Generation 4    most core NT services moved to NTLinux
Generation 5    wineserver becomes optional or minimal
```

Compatibility is more important than architectural purity.

---

# 4. Native PE execution

Windows binaries should behave like normal executable files.

Desired UX:

```bash
./game.exe
./setup.exe
./tool.exe
```

rather than:

```bash
wine game.exe
```

Use a Linux binary-format registration mechanism to dispatch PE binaries to:

```text
/usr/libexec/ntloader
```

`ntloader` responsibilities:

```text
read PE headers
reserve address space
map PE image sections
establish process environment
construct PEB
construct TEB
initialize ntdll
load dependencies
initialize Win32 runtime
transfer control to PE entry point
```

The program still executes directly on the host CPU when architecture-compatible.

No CPU emulator should be involved for normal x86/x64 Windows applications.

---

# 5. NT host ABI

Create a stable interface between Windows semantics and Linux.

Suggested components:

```text
/dev/nt
libntabi.so
ntd
```

Possible early architecture:

```text
ntdll
  │
libntabi
  │
shared-memory request queues
  │
ntd
  │
Linux
```

As performance-critical operations mature:

```text
ntdll
  │
ioctl/mmap/syscall-style interface
  │
Linux NT facilities
```

Do not allocate hundreds of custom Linux syscalls during early development.

Prefer a versioned protocol.

---

# 6. NT object model

Windows software expects object semantics that are not equivalent to ordinary POSIX descriptors.

Important objects include:

```text
Event
Semaphore
Mutant
Timer
Section
Process
Thread
Job
File
Directory Object
Symbolic Link Object
Completion Port
Token
Key
Device
Driver
```

An NTLinux object service should provide:

```text
handle table
reference counting
object lifetime
named objects
security descriptors
waitability
namespace lookup
inheritance
cross-process handle duplication
```

Expected namespace model:

```text
\
├── Device
├── Sessions
├── BaseNamedObjects
├── KnownDlls
├── RPC Control
└── ??
    ├── C:
    ├── D:
    └── UNC
```

Do not reduce NT object paths to Unix path strings too early.

---

# 7. Synchronization

Prefer Linux's NT-oriented synchronization facilities where available.

Important semantics include:

```text
events
mutexes / mutants
semaphores
timers
wait-any
wait-all
alertable waits
APCs
thread suspension
```

Avoid inefficient polling or excessive userspace server round-trips.

Where Linux provides primitives specifically intended to represent NT synchronization, use them as foundational infrastructure.

---

# 8. Filesystem compatibility

Windows filesystem semantics are significantly richer/different than simple POSIX translation.

Required behavior includes:

```text
drive letters
NT paths
DOS paths
UNC paths
case-insensitive lookup
share modes
delete-pending state
reparse points
file IDs
oplocks
alternate streams
security descriptors
DOS device names
volume information
file disposition semantics
```

Conceptual design:

```text
NT path
   │
NT VFS semantics
   │
host path resolver
   │
Linux VFS
```

Example mappings:

```text
C:   -> /run/nt/drives/c
Z:   -> /
UNC  -> SMB/VFS resolver
```

Do not require the root filesystem to be NTFS.

---

# 9. GUI integration

Do not recreate the Windows graphics kernel unless strictly necessary for compatibility.

User-mode Windows windows should become normal Wayland surfaces.

Target:

```text
user32
  │
win32u
  │
NTLinux window backend
  │
Wayland
  │
compositor
```

Potential future compositor:

```text
ntcomp
```

Desired behavior:

- HWND maps cleanly to host surfaces.
- Windows and Linux applications coexist in one desktop.
- clipboard is shared.
- drag/drop is shared.
- file pickers integrate.
- notifications integrate.
- taskbar/dock integration is native.
- DPI/scaling follows host policy.
- IME/input methods integrate with Linux.

---

# 10. Gaming stack

Do not rewrite Direct3D.

Use:

```text
D3D9
D3D10
D3D11
   │
   └── DXVK
         │
       Vulkan
```

and:

```text
D3D12
   │
vkd3d-proton
   │
Vulkan
```

Also integrate:

```text
DXVK-NVAPI
Gamescope
HDR
VRR
FSR
DLSS exposure where supported
Steam Input
XInput
GameInput
PipeWire
evdev
hidraw
```

Proton should become a compatibility/profile layer on top of NTLinux rather than the fundamental Windows runtime.

Long-term conceptual path:

```text
Steam
  │
Proton
  │
NTLinux runtime
  │
NTLinux NT ABI
  │
Linux
```

---

# 11. WoW64

x86-64 is the initial host target.

Support:

```text
64-bit PE -> native x86-64 execution
32-bit PE -> WoW64 compatibility path
```

Reuse modern Wine WoW64 work wherever possible.

ARM64 support is a later milestone.

---

# 12. Windows driver support

Windows kernel drivers are a core project goal.

Do not independently recreate WDM, the NT I/O Manager, Object Manager, PnP Manager, IRP engine and kernel-driver environment if ReactOS already implements them.

The intended solution is a **hosted ReactOS NT kernel environment**.

---

# 13. ReactOS is the NT driver runtime

ReactOS already contains major implementations of:

```text
NT Executive
Object Manager
I/O Manager
IRP infrastructure
PnP Manager
Power Manager
Memory Manager
Security Manager
Configuration Manager
WDM
device stacks
driver loading
registry integration
NT synchronization
HAL abstractions
```

Therefore:

```text
DO NOT:
rewrite all of ntoskrnl as libntdriver.so

DO:
adapt ReactOS so its NT kernel can be hosted by Linux
```

---

# 14. Hosted ReactOS kernel

The preferred first driver implementation is a minimal ReactOS kernel running in a KVM cell.

Concept:

```text
Linux
  │
 KVM
  │
┌─────────────────────────────┐
│ ReactOS NT Driver Cell      │
│                             │
│ HAL                         │
│ ntoskrnl                    │
│ Object Manager              │
│ I/O Manager                 │
│ PnP                         │
│ Memory Manager              │
│ Registry                    │
│ WDM                         │
│ KMDF                        │
│ NDIS                        │
│ Windows .sys drivers        │
└──────────────┬──────────────┘
               │
          NT bridge
               │
         VFIO / IOMMU
               │
           hardware
```

This avoids needing to emulate CPU ring-0 semantics inside an ordinary Linux process.

---

# 15. ReactOS Driver Cell

Create a special ReactOS build profile whose purpose is only to host drivers.

Working name:

```text
ReactOS Driver Cell
```

Possible internal build name:

```text
ROS-NTCELL
```

It should boot only what is needed for driver execution:

```text
boot loader
HAL
ntoskrnl
registry
PnP
I/O Manager
Object Manager
Memory Manager
driver loader
selected system drivers
bridge transport
```

Exclude where possible:

```text
Explorer
desktop shell
Winlogon
full GDI desktop
interactive session
browser
general ReactOS applications
```

Target footprint should eventually be small enough that multiple driver cells are practical.

---

# 16. Why use KVM initially

Many Windows `.sys` drivers assume they execute in kernel mode.

They may depend on:

```text
CPL0
CR0 / CR3 / CR4
MSRs
IDT
GDT
KPCR
KPRCB
KTHREAD
IRQL
DPC
APC
spinlocks
page tables
kernel virtual address conventions
interrupt context
physical memory APIs
```

Trying to perfectly reproduce all of these in Linux userspace is effectively another kernel project.

KVM allows ReactOS to provide the environment it already implements.

Therefore:

```text
Windows .sys
    │
actual NT kernel semantics
    │
ReactOS ntoskrnl
    │
virtualized CPU
    │
KVM
```

instead of:

```text
Windows .sys
    │
huge userspace emulation layer
    │
Linux
```

---

# 17. NT driver bridge

Create a high-performance host/cell protocol.

Possible name:

```text
ntbridge
```

Architecture:

```text
Linux host
    │
ntbridge-host
    │
shared memory
eventfd
ring buffers
    │
KVM boundary
    │
ntbridge-ros
    │
ReactOS kernel
```

Use separate queues where useful:

```text
control
PnP
MMIO
interrupt
DMA
network
storage
USB
logging
power
device lifecycle
```

The protocol must be versioned.

---

# 18. Device discovery

Linux remains responsible for physical enumeration.

Example:

```text
PCI/USB hardware
    │
Linux
    │
sysfs / udev / netlink
    │
NTLinux device broker
    │
ntbridge
    │
ReactOS PnP Manager
    │
PDO
    │
FDO
    │
filter stack
    │
Windows driver
```

ReactOS remains responsible for the **NT view** of the device tree.

Linux provides the physical facts.

---

# 19. Preserve the NT PnP model

Do not replace ReactOS PnP with udev.

Instead:

```text
udev event
   │
Linux host adapter
   │
ReactOS PnP Manager
   │
NT DEVICE_NODE
   │
PnP IRPs
   │
Windows driver
```

Support normal Windows PnP concepts:

```text
PDO
FDO
filter DO
bus driver
function driver
filter driver
device relations
hardware IDs
compatible IDs
resources
start/stop/remove
power transitions
```

---

# 20. IRPs

Do not reinvent the IRP engine.

ReactOS already implements the model.

Expected driver-facing flow:

```text
I/O request
    │
ReactOS I/O Manager
    │
IRP
    │
filter driver
    │
function driver
    │
bus driver
    │
host bridge
```

Core calls include:

```text
IoCreateDevice
IoAttachDeviceToDeviceStack
IoAllocateIrp
IoBuildDeviceIoControlRequest
IoCallDriver
IoCompleteRequest
IoCancelIrp
IoSetCompletionRoutine
```

Keep their semantics inside the ReactOS kernel wherever practical.

---

# 21. KMDF

Prefer implementing/finishing KMDF behavior inside ReactOS rather than creating an NTLinux-only KMDF clone.

Driver-facing objects include:

```text
WDFDRIVER
WDFDEVICE
WDFQUEUE
WDFREQUEST
WDFMEMORY
WDFINTERRUPT
WDFDMAENABLER
WDFTIMER
WDFWORKITEM
WDFIOTARGET
```

If modern Windows drivers require newer WDF behavior that ReactOS lacks:

1. implement it in ReactOS,
2. upstream it where feasible,
3. consume it from NTLinux.

This avoids permanent forks of Windows kernel semantics.

---

# 22. NDIS

Where practical, allow Windows network drivers to execute as NDIS miniports inside the ReactOS cell.

Host-facing architecture:

```text
Windows NIC driver
      │
     NDIS
      │
ReactOS network bridge
      │
shared packet rings
      │
Linux host
      │
netdev / TAP / AF_XDP
      │
Linux TCP/IP
```

NTLinux does not need to route ordinary host networking through the Windows TCP/IP stack.

The Windows driver can terminate at an NDIS-to-Linux network bridge.

---

# 23. Interrupts

Physical interrupts remain controlled by Linux.

Example:

```text
device MSI/MSI-X
      │
Linux IRQ handling
      │
VFIO
      │
eventfd
      │
ntbridge
      │
ReactOS virtual interrupt
      │
Windows ISR
      │
DPC
```

ReactOS maintains the Windows interrupt/DPC semantics visible to the driver.

Linux controls the physical device and isolation boundary.

---

# 24. DMA

Never allow a foreign Windows driver unrestricted access to host physical memory.

Use the host IOMMU.

Concept:

```text
Windows driver
      │
NT DMA APIs
      │
ReactOS MM/HAL
      │
ntbridge
      │
IOMMUFD / VFIO
      │
IOVA
      │
device DMA
```

Translate Windows concepts such as:

```text
MDL
scatter/gather list
DMA adapter
logical addresses
common buffers
```

into protected host mappings.

A buggy `.sys` should not be able to DMA into arbitrary Linux memory.

---

# 25. MMIO and port I/O

MMIO should be exposed through controlled VFIO-backed regions.

```text
Windows driver
   │
MmMapIoSpace
   │
ReactOS
   │
ntbridge
   │
VFIO BAR mapping
```

Port I/O and privileged instructions should remain inside the virtualized driver environment where possible.

Do not expose Linux kernel privileges directly to Windows binaries.

---

# 26. Driver failure isolation

One major architectural advantage of the driver-cell design is fault isolation.

Traditional model:

```text
bad kernel driver
      │
entire OS crashes
```

NTLinux target:

```text
bad Windows driver
      │
ReactOS driver cell crashes
      │
Linux survives
      │
device is reset
      │
cell may be restarted
```

The host must assume every foreign `.sys` is untrusted.

---

# 27. Driver-cell granularity

Support multiple isolation strategies over time.

## Mode A — one global NT driver cell

Simplest early implementation.

```text
all Windows .sys drivers
       │
single ReactOS kernel
```

## Mode B — one cell per device class

Example:

```text
GPU cell
Wi-Fi cell
USB device cell
special hardware cell
```

## Mode C — one cell per device/driver

Maximum isolation.

```text
device A -> cell A
device B -> cell B
```

The project should not require Mode C initially.

---

# 28. ReactOS upstream strategy

Whenever a missing Windows kernel behavior is generic NT functionality:

```text
modern WDM behavior
KMDF completion
new kernel export
PnP behavior
memory-manager behavior
NDIS behavior
security semantics
NT 6+ behavior
```

prefer implementing it in ReactOS itself.

NTLinux-specific code should mainly be:

```text
Linux host backend
KVM launcher
ntbridge
VFIO integration
IOMMU integration
udev/sysfs translation
Linux desktop integration
NT user ABI integration
packaging
```

---

# 29. ReactOS is not automatically modern Windows

ReactOS provides an enormous head start, but it must not be treated as though it already equals a modern Windows 10/11 kernel.

Expect missing or incomplete behavior in areas such as:

```text
newer NT kernel interfaces
modern WDF
modern NDIS
WDDM
newer power management
recent PnP behavior
security features
modern driver signing
newer memory-manager APIs
newer kernel exports
GPU driver infrastructure
```

NTLinux should maintain a compatibility matrix by Windows subsystem/API generation.

---

# 30. Graphics drivers

Initial NTLinux gaming should use Linux GPU drivers.

Preferred route:

```text
Windows game
   │
Direct3D
   │
DXVK / vkd3d-proton
   │
Vulkan
   │
Linux Mesa/NVIDIA driver
```

Do not make Windows WDDM GPU drivers a prerequisite for the distro.

Windows GPU-driver execution should be a research milestone, not an MVP dependency.

---

# 31. Audio

Map Windows audio APIs onto PipeWire.

```text
WASAPI
XAudio2
DirectSound
MME
    │
Windows audio layer
    │
PipeWire
```

Keep native Linux audio drivers.

---

# 32. Input

Use Linux's existing input stack.

```text
XInput
GameInput
Raw Input
DirectInput
HID
   │
NTLinux input bridge
   │
evdev / hidraw
```

Game controller identity and capability mapping should be handled centrally.

---

# 33. Services

Windows services should be first-class managed workloads.

Provide compatibility for:

```text
SCM
service dependencies
service control messages
startup types
service accounts
named pipes
service registry entries
```

Implementation may initially remain Wine-compatible but should eventually integrate with the NTLinux service manager.

Potential host mapping:

```text
Windows SCM model
       │
ntsvc
       │
systemd transient/service units
```

Do not expose systemd semantics directly to Windows applications.

---

# 34. Registry

Provide a central NT registry service.

Expected namespaces:

```text
HKLM
HKCU
HKCR
HKU
HKCC
```

The backing store does not have to resemble Windows registry hive files internally.

However, Windows-visible semantics should be preserved.

Driver cells need access to a consistent registry view for:

```text
services
driver configuration
PnP
class keys
hardware profiles
device parameters
```

---

# 35. COM and RPC

Reuse Wine's existing COM/RPC implementation initially.

Long-term objective:

```text
Windows COM
Windows RPC
named pipes
ALPC-like services
cross-process activation
```

should integrate cleanly with the NTLinux object and IPC model.

Do not rewrite COM during early development.

---

# 36. Compatibility profiles

Applications should be able to declare behavior profiles.

Example:

```text
Windows 7
Windows 10
Windows 11
Steam/Proton
legacy XP
strict NT
```

Profiles may select:

```text
DLL versions
quirks
registry overrides
filesystem behavior
graphics configuration
driver-cell requirements
anti-cheat compatibility settings
```

---

# 37. Per-application environments

Avoid traditional Wine-prefix complexity being visible to ordinary users.

Internally, each application may still have an isolated environment:

```text
~/.local/share/ntlinux/apps/<id>/
├── drive-c/
├── registry/
├── config.toml
├── compat/
├── dll-overrides/
└── state/
```

But user-facing installation should behave like installing a normal Linux application.

---

# 38. Steam / Proton integration

Steam should automatically detect NTLinux as a native compatibility host.

Potential flow:

```text
Steam
  │
game manifest
  │
NTLinux compatibility selector
  │
Proton components
  │
NTLinux runtime
```

The Proton fork should gradually lose responsibility for low-level NT translation as NTLinux provides those facilities directly.

Keep game-specific patches separate from generic NT behavior.

---

# 39. Security model

Treat Windows applications and drivers as untrusted workloads.

Applications should be constrainable using:

```text
namespaces
seccomp
cgroups
Landlock
filesystem sandboxing
network namespaces
capability dropping
```

Driver cells additionally use:

```text
KVM isolation
VFIO
IOMMU
memory limits
CPU limits
device allowlists
shared-memory validation
watchdogs
```

Never allow a Windows driver binary to load directly as a Linux kernel module.

---

# 40. Anti-cheat and DRM

Do not promise universal anti-cheat compatibility.

Separate cases:

```text
user-mode anti-cheat
Proton-aware anti-cheat
kernel driver anti-cheat
hardware-backed security
attestation-based systems
```

Kernel anti-cheat may technically execute inside the ReactOS driver cell while still failing because the environment is not genuine Windows or does not provide expected attestation/security properties.

Treat this as a compatibility problem, not something to bypass.

---

# 41. Proposed repository

```text
ntlinux/
│
├── distro/
│   ├── image/
│   ├── installer/
│   ├── packages/
│   └── rootfs/
│
├── kernel/
│   ├── config/
│   ├── patches/
│   └── nt/
│
├── ntabi/
│   ├── include/
│   ├── protocol/
│   ├── lib/
│   └── tests/
│
├── ntloader/
│
├── ntd/
│   ├── objects/
│   ├── registry/
│   ├── services/
│   ├── namespace/
│   ├── security/
│   └── rpc/
│
├── runtime/
│   ├── wine/
│   ├── ntdll/
│   ├── win32u/
│   └── wow64/
│
├── graphics/
│   ├── dxvk/
│   ├── vkd3d-proton/
│   └── dxvk-nvapi/
│
├── proton/
│
├── driver/
│   ├── cell/
│   │   ├── launcher/
│   │   ├── images/
│   │   └── config/
│   │
│   ├── ntbridge/
│   │   ├── protocol/
│   │   ├── host/
│   │   └── reactos/
│   │
│   ├── vfio/
│   ├── iommu/
│   ├── pnp-host/
│   ├── net/
│   ├── storage/
│   └── usb/
│
├── reactos/
│   ├── patches/
│   └── upstream/
│
├── desktop/
│   ├── wayland/
│   ├── shell/
│   └── integration/
│
├── tooling/
│   ├── compat-db/
│   ├── traces/
│   ├── debugger/
│   └── installer/
│
└── tests/
    ├── nt-api/
    ├── win32/
    ├── wine/
    ├── proton/
    ├── reactos/
    ├── drivers/
    └── games/
```

---

# 42. Suggested component names

These are placeholders and can be renamed later.

```text
ntloader       PE executable launcher
ntd            NT userspace service daemon
libntabi       NT/Linux ABI client
ntbridge       Linux <-> ReactOS transport
ntcell         ReactOS driver-cell launcher
ntpnpd         physical-device discovery adapter
ntregd         registry service
ntsvc          Windows service manager
ntcomp         NT-aware Wayland compositor integration
ntcompat       compatibility database/runtime selector
```

---

# 43. Build phases

## Phase 0 — baseline distro

Deliver:

```text
Linux kernel
Wayland
PipeWire
Mesa
Steam
Wine
Proton
DXVK
vkd3d-proton
ntsync support
```

Goal:

A fully usable gaming Linux distro before custom NT architecture exists.

---

## Phase 1 — native Windows executable UX

Deliver:

```text
ntloader
PE registration
per-app environment management
desktop file generation
Windows application installer
```

Success criterion:

```bash
./notepad.exe
```

works without explicitly invoking Wine.

---

## Phase 2 — NT ABI prototype

Deliver:

```text
libntabi
ntd
versioned protocol
basic handles
events
mutexes
semaphores
sections
basic virtual-memory operations
```

Route selected `ntdll` functionality through it.

Success criterion:

Wine test suites continue passing while some core NT operations no longer use the normal Wine path.

---

## Phase 3 — object and wait semantics

Move:

```text
named objects
handle tables
wait-any
wait-all
sections
shared memory
process/thread metadata
completion ports
APC support
```

toward NTLinux-native implementation.

Measure reduction in wineserver IPC.

---

## Phase 4 — ReactOS driver-cell prototype

Deliver:

```text
minimal bootable ReactOS image
KVM launcher
ntbridge
logging channel
host heartbeat
device enumeration bridge
```

Success criterion:

ReactOS sees synthetic devices supplied by Linux.

---

## Phase 5 — first Windows driver

Start with a simple virtual or low-risk device.

Good candidates:

```text
virtual serial device
virtual block device
simple USB device
virtual network adapter
```

Avoid GPU drivers initially.

Success criterion:

A real Windows `.sys` loads, receives PnP IRPs and successfully performs I/O through the host bridge.

---

## Phase 6 — VFIO hardware passthrough

Add:

```text
PCI BAR mapping
MSI/MSI-X delivery
DMA mappings
device reset
IOMMU protection
resource descriptors
```

Success criterion:

A selected real Windows PCI driver operates physical hardware while Linux remains stable if the cell crashes.

---

## Phase 7 — NDIS bridge

Deliver:

```text
Windows NDIS miniport
ReactOS networking side
shared packet transport
Linux netdev
```

Success criterion:

A Windows NIC driver exposes a working Linux network interface.

---

## Phase 8 — USB driver bridge

Provide:

```text
USB enumeration
URB translation/bridging
PnP
hotplug
power events
```

Success criterion:

A vendor Windows USB driver can operate a device unavailable through a native Linux driver.

---

## Phase 9 — modern ReactOS driver compatibility

Focus upstream work on:

```text
KMDF
newer WDM
newer NT exports
modern NDIS
PnP
power
memory management
security
```

Maintain conformance tests against documented Windows behavior.

---

## Phase 10 — gaming integration

Tight integration between:

```text
Steam
Proton
NTLinux runtime
Gamescope
DXVK
vkd3d-proton
driver compatibility database
```

Windows applications and games should feel like native Linux packages.

---

# 44. Testing strategy

Testing must be differential wherever possible.

Compare:

```text
Windows
Wine
ReactOS
NTLinux
```

for the same API calls.

Capture:

```text
return values
NTSTATUS
GetLastError
object lifetime
handle behavior
timing
wait semantics
filesystem state
registry state
IRP sequences
PnP sequences
driver callbacks
```

Create a dedicated compatibility-test corpus.

---

# 45. Tracing

Build tracing into the architecture from the start.

Example:

```text
NTCALL NtCreateFile(...)
NTCALL NtCreateSection(...)
OBJECT Event created ...
IRP IRP_MJ_CREATE -> driver.sys
IRP IRP_MN_START_DEVICE -> driver.sys
PNP device state ...
DMA map ...
IRQ vector ...
```

Tracing should support:

```text
human-readable logs
binary trace files
Chrome/Perfetto trace export
comparison against Windows traces
replay where possible
```

---

# 46. Compatibility database

Maintain a central database:

```text
application
game
driver
version
Windows target
required overrides
known bugs
required NT features
required Proton patches
driver-cell requirements
test status
```

Example states:

```text
native
works
works-with-profile
partial
blocked
driver-cell-required
unsupported
```

---

# 47. Performance goals

The architecture should avoid becoming:

```text
Windows app
  -> Wine
  -> daemon
  -> kernel
  -> VM
  -> daemon
  -> Linux
```

for normal application calls.

The ReactOS/KVM cell exists primarily for **kernel-driver semantics**, not ordinary Win32 execution.

Normal games should remain:

```text
game machine code
      │
Wine/NTLinux user runtime
      │
DXVK/vkd3d
      │
Vulkan
      │
Linux GPU driver
```

The driver cell should only be crossed when the application actually needs an emulated Windows kernel device.

---

# 48. IPC performance

For host/cell communication prefer:

```text
shared memory
lock-free or low-lock rings
eventfd
batching
zero-copy packet transfer
zero-copy block I/O where safe
doorbell-style notifications
```

Avoid one VM exit per tiny logical NT operation where batching is possible.

---

# 49. Long-term native NT kernel facilities in Linux

Only move semantics into Linux kernel space when profiling proves the benefit.

Candidates:

```text
NT wait primitives
section/shared-memory operations
high-frequency handle signaling
completion-port primitives
selected object operations
```

Do not move:

```text
registry
COM
service management
compatibility policy
application quirks
large amounts of NT object policy
```

into the kernel without a compelling reason.

---

# 50. What NTLinux is not

NTLinux is not:

```text
a Windows clone
ReactOS with a Linux theme
Wine preinstalled on Ubuntu
a full-system Windows VM
a new Direct3D implementation
a replacement Linux kernel
a project to load .sys files directly into Linux kernel space
```

It is:

> A Linux operating system that treats the NT execution model as a first-class personality, reusing Wine for Windows userland, Proton for games, ReactOS for NT kernel/driver semantics, and Linux/KVM/VFIO for safe hardware execution.

---

# 51. Architectural ownership

## Linux owns

```text
hardware
host CPU scheduling
physical memory
IOMMU
PCI
USB host stack
DRM/KMS
native GPU driver
Vulkan
filesystems
network stack
audio hardware
input hardware
KVM
VFIO
security boundaries
```

## Wine owns initially

```text
Win32 DLLs
user32
gdi32
COM
WinSock translation
Windows user-mode runtime
loader components
WoW64
```

## Proton owns

```text
Steam compatibility
DXVK integration
vkd3d-proton
game patches
runtime/container behavior
game-specific environment
```

## ReactOS owns

```text
NT kernel semantics
WDM
I/O Manager
Object Manager
IRPs
PnP Manager
device stacks
kernel synchronization semantics
driver loader
NT memory-manager contract
kernel security interfaces
Windows driver environment
```

## NTLinux owns

```text
integration
NT/Linux ABI
PE-native execution experience
Linux host adaptation
ReactOS driver-cell hosting
ntbridge
VFIO/IOMMU mapping
device discovery translation
desktop integration
packaging
compatibility profiles
security policy
orchestration
```

---

# 52. Rules for contributors and coding agents

## Rule 1

Before implementing a Windows API or NT kernel facility, search upstream Wine and ReactOS.

## Rule 2

Do not create a duplicate NT subsystem merely because adapting upstream code appears inconvenient.

## Rule 3

Prefer patches that can be upstreamed to Wine, ReactOS, Proton or Linux.

## Rule 4

Keep Linux-specific code outside generic ReactOS NT logic whenever possible.

## Rule 5

Never load untrusted Windows `.sys` code directly into the Linux kernel.

## Rule 6

Hardware access from a Windows driver must be mediated through VFIO, IOMMU or an equivalent safe host facility.

## Rule 7

Normal Win32 execution must not require a KVM guest.

## Rule 8

The ReactOS driver cell exists for kernel-driver execution, not as a hidden Windows desktop VM.

## Rule 9

Do not rewrite DXVK, vkd3d-proton, Mesa, PipeWire or other mature host subsystems.

## Rule 10

Compatibility regressions are more important than architectural elegance.

## Rule 11

Every NT compatibility implementation requires tests.

## Rule 12

Cross-boundary protocols must be versioned.

## Rule 13

Prefer shared-memory transports over chatty RPC for hot I/O paths.

## Rule 14

All foreign driver execution is considered untrusted.

## Rule 15

Document which project owns every compatibility behavior.

---

# 53. Immediate implementation targets

Start here:

```text
1. Create base distro image.
2. Package current Wine + Proton stack.
3. Enable NT-oriented Linux synchronization support.
4. Implement ntloader.
5. Make PE binaries directly executable.
6. Build per-application NT environment manager.
7. Implement libntabi protocol skeleton.
8. Route one small set of ntdll operations through it.
9. Create ReactOS minimal driver-cell build experiment.
10. Boot that build under KVM with no desktop.
11. Create ntbridge shared-memory hello/heartbeat protocol.
12. Send synthetic PCI/USB device descriptions from Linux to ReactOS.
13. Confirm ReactOS PnP creates device nodes.
14. Load a simple test `.sys`.
15. Deliver IRP_MN_START_DEVICE.
16. Bridge test I/O back to Linux.
```

---

# 54. First driver-cell milestone

The initial end-to-end target should be deliberately simple:

```text
Linux
  │
creates virtual test device
  │
ntbridge
  │
ReactOS PnP
  │
loads testdriver.sys
  │
DriverEntry
  │
AddDevice
  │
IRP_MN_START_DEVICE
  │
IRP_MJ_DEVICE_CONTROL
  │
ntbridge
  │
Linux test client
```

Success means the architecture works before real hardware is involved.

---

# 55. Second driver-cell milestone

After the synthetic device works:

```text
physical PCI device
      │
VFIO
      │
Linux host bridge
      │
ReactOS cell
      │
Windows vendor driver
```

The first real hardware should be:

- non-critical,
- easily resettable,
- isolated by IOMMU,
- not the boot disk,
- not the primary GPU,
- not the host's only network interface.

---

# 56. Long-term vision

Eventually a user should be able to install software without caring which ecosystem it came from.

```text
Linux ELF
Windows PE
Steam Windows game
Windows hardware utility
Windows device driver
```

all become managed workloads of one Linux system.

Target UX:

```text
install program
launch program
plug device in
driver is selected
application opens
```

without exposing:

```text
Wine prefixes
manual DLL overrides
manual Proton commands
VM windows
ReactOS desktop
driver-cell internals
```

unless the user enters developer mode.

---

# 57. Final architecture

```text
                                  NTLinux
                                     │
                 ┌───────────────────┴───────────────────┐
                 │                                       │
             Linux ABI                               NT ABI
                 │                                       │
          Linux applications                       Windows PE
                                                         │
                                                  Wine Win32 layer
                                                         │
                                                      ntdll
                                                         │
                                                  NTLinux NT ABI
                                                         │
                       ┌─────────────────────────────────┴─────────────┐
                       │                                               │
                 Linux host services                           Windows driver path
                       │                                               │
       ┌───────────────┼──────────────┐                          ntbridge
       │               │              │                              │
    Wayland         PipeWire       Vulkan                           KVM
       │               │              │                              │
    compositor       audio       Linux GPU driver            ReactOS NT kernel
                                                                      │
                                                              WDM / KMDF / NDIS
                                                                      │
                                                               Windows .sys
                                                                      │
                                                                 VFIO/IOMMU
                                                                      │
                                                                  hardware
```

The defining principle is:

> **Do not replace Windows with Linux translations everywhere, and do not rebuild Windows from scratch. Reuse the NT implementation where it already exists, then host that implementation safely on Linux.**

That is the architectural identity of NTLinux.
