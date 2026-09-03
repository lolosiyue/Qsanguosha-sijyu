# Windows XP SP3 / Win7 x86 legacy build

## Support boundary

| Item | XP legacy product |
|---|---|
| OS / architecture | Windows XP SP3 x86 and Windows 7 x86 (same PE32 binary and portable payload) |
| Supported room size | 2–10 players |
| 20-player room | Connection may be attempted; compatibility is not promised |
| Executable | One `QSanguoshaXP.exe`; use `-server` to host |
| UI | Existing `QGraphicsScene` / `StartScene` classic UI |
| Effects | Forced to `NONE`; the setting is hidden |
| Audio | x86 FMOD Ex 4.44.53 in both Debug and Release |
| Excluded features | QML, Spine, video, OpenGL, and the WebSocket gateway |
| Excluded content | External `extensions/`; it is outside the XP compatibility promise |
| Listen path | Native TCP `9527` only. Compact web / port `9528` is not part of this product |
| Distribution | Portable folder or Joliet ISO with `INSTALL.CMD` |

This is an opt-in legacy product. The normal `debug` target remains the Qt 6.11
x64 development build and does not inherit the XP toolchain or feature cuts.

There is no separate Win7 build tier. The `v141_xp` / Qt 5.6.3 x86 artifact is
the only legacy deliverable; Win7 x86 is covered by upward compatibility of the
same portable folder or ISO `PAYLOAD/` tree.

## Toolchain and Qt baseline

- Visual Studio 2017 Build Tools 15.9, MSVC 14.16
- `v141_xp`, Win32
- Windows SDK 7.1A system libraries plus the v141 Universal CRT
- Official Qt 5.6.3 MSVC 2015 x86 development/runtime tree
- `/Zc:threadSafeInit-` for the XP target

Pass the Qt tree explicitly. It must contain `bin/qmake.exe`, the Qt CMake
packages, Release and Debug DLLs, and the required plugins:

```powershell
$qt56 = "C:\Qt\5.6.3\msvc2015"
powershell -NoProfile -ExecutionPolicy Bypass `
  -File legacy/xp/tools/build-xp.ps1 `
  -Configuration Release -QtRoot $qt56
```

The official Qt DLLs stamp PE OS/subsystem 6.00 but have been exercised in the
XP SP3 x86 VM. The gate therefore requires exact PE 5.01 for the project EXE
and FMOD, and validates vendor Qt DLLs by x86 machine type, forbidden post-XP
direct imports, exact Qt 5.6.3 version and guest runtime acceptance.

`legacy/xp/tools/build-qt56-xp.ps1` remains available to reproduce a source
build for diagnosis. It verifies the official source archive MD5 and applies
the XP USER32 resolver patch plus `/Zc:threadSafeInit-`; it is not the accepted
runtime baseline until it passes the same guest GUI gate.

## Build and deploy

Debug and Release use the same feature set. FMOD runtime names must remain
`fmodexL.dll` for Debug and `fmodex.dll` for Release:

```powershell
$qt56 = "C:\Qt\5.6.3\msvc2015"

powershell -NoProfile -ExecutionPolicy Bypass `
  -File legacy/xp/tools/build-xp.ps1 `
  -Configuration Debug -QtRoot $qt56 -Deploy `
  -FmodRuntime C:\approved-runtime\fmodexL.dll `
  -AssetRoot C:\QSanguosha-assets `
  -DeployRoot C:\out\QSanguoshaXP-debug

powershell -NoProfile -ExecutionPolicy Bypass `
  -File legacy/xp/tools/build-xp.ps1 `
  -Configuration Release -QtRoot $qt56 -Deploy `
  -FmodRuntime C:\approved-runtime\fmodex.dll `
  -AssetRoot C:\QSanguosha-assets `
  -DeployRoot C:\out\QSanguoshaXP-release
```

The deterministic deploy target copies Qt, plugins, VC/UCRT DLLs, FMOD and
supported runtime assets. It removes QML/video files and external extensions.
FMOD binaries must come from a distribution source whose licence has been
approved; they are not committed by this branch.

Server mode uses the same executable:

```powershell
QSanguoshaXP.exe -server
```

## ISO media

Create the XP-compatible ISO from a completed portable Release folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File legacy/xp/tools/new-xp-iso.ps1 `
  -SourceDirectory C:\out\QSanguoshaXP-release `
  -IsoPath C:\out\QSanguosha-XP-SP3-x86.iso `
  -VolumeLabel QSAN_XP
```

The ISO keeps the portable tree under `PAYLOAD/`. `INSTALL.CMD` first leaves
the target directory, then performs a clean `xcopy /E` installation to
`C:\QSanguoshaXP`. CAB extraction is intentionally not used because XP
`expand.exe` flattens destination subdirectories. `-ReuseStage` may be used
when only `AUTORUN.INF`, `INSTALL.CMD` or `RUNXP.CMD` changed.

## Win7 x86 VM acceptance (same portable payload)

Win7 x86 validation reuses the exact portable Release folder produced by
`-Deploy` (or the ISO `PAYLOAD/` extracted from that folder). Do not build,
deploy, or gate a second Win7-specific payload.

Recommended guest checks on Windows 7 x86 SP1:

1. copy or install the same portable tree used for XP acceptance;
2. launch `QSanguoshaXP.exe` and confirm the classic `StartScene` main window;
3. start `-server` on the guest and connect with `-connect:127.0.0.1`;
4. exercise 125% system DPI scaling and confirm the window remains usable;
5. run `legacy/xp/tools/check-xp-pe.ps1` against the deployed root to confirm
   the tree is still x86 and free of forbidden post-XP direct imports.

Win7-specific regressions (DPI, UAC, audio device enumeration) are tracked
separately from the XP SP3 guest evidence below; passing XP acceptance does not
by itself close the Win7 x86 gate.

## Acceptance evidence and residual limits

On 2026-08-31 the Release bundle passed these checks in a Windows XP SP3 x86
VirtualBox guest:

1. clean ISO installation preserved `lua/config.lua` and
   `platforms/qwindows.dll`;
2. the classic `StartScene` reached the main window with effects forced to
   `NONE`;
3. the same EXE started a local server and a client using
   `-connect:127.0.0.1` entered the waiting room;
4. Qt 5.6.3, QtNetwork and FMOD loaded without an XP loader failure.

VirtualBox 7.0.2 on the host crashed when VM audio was enabled, so the VM used
`audio=none`. FMOD load/initialization is covered, but audible playback remains
a physical/alternate-hypervisor acceptance gate. Rooms above 10 players,
external extensions and 20-player memory/load behavior remain outside the
compatibility commitment.

## VirtualBox Guest Control automation

Use `tools/xp-vm-control.ps1` for the repeatable host-to-guest operations. It
starts the known VM, waits for Guest Additions, runs console commands, copies
files, launches a GUI command in the visible XP desktop, shuts the VM down, and
can explicitly restore a named snapshot. It does not change the VM network or
take screenshots.

Create the ignored, current-Windows-user-bound credential once. Do not commit
the generated XML or any plaintext password file:

```powershell
Get-Credential CodexQA |
  Export-Clixml builds\xp-poc\CodexQA.credential.xml
```

Start the VirtualBox console and wait for usable Guest Control:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action Start

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action Status
```

Use `RunGuest` for non-GUI commands and logs:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action RunGuest `
  -GuestCommand C:\WINDOWS\system32\cmd.exe `
  -GuestArguments /c,type,C:\QSanguoshaXP\client.log

New-Item -ItemType Directory builds\xp-poc\guest-logs -Force
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action CopyFromGuest `
  -GuestPath C:\QSanguoshaXP\client.log `
  -HostPath builds\xp-poc\guest-logs
```

`CopyFromGuest` deliberately requires an existing host destination directory;
passing a nonexistent filename as the destination causes VirtualBox
`VERR_FILE_NOT_FOUND`.

### Visible XP desktop

The current VM's visible Explorer desktop logs in as `Administrator`, while
Guest Control authenticates as `CodexQA`. A normal `guestcontrol run/start`
GUI process therefore exists on a non-visible desktop even when `tasklist`
reports session `Console`.

Put the desired working-directory and arguments in a guest `.cmd` file, copy it
to a path without spaces, then schedule it through XP `AT /interactive`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action RunInteractive `
  -GuestCommandLine C:\QSanguoshaXP\run-xp-local-gui.cmd `
  -ExpectedProcess QSanguoshaXP.exe
```

The command runs at the next guest minute and can take up to 60 seconds to
appear. The script records existing PIDs and polls `tasklist` for a new process
when `-ExpectedProcess` is supplied.
VirtualBox 7.0.2 can inject keyboard scan codes and capture screenshots but has
no `controlvm mouseputstate`; prefer Guest Control, application logs and
deterministic command files, using screenshots only at UI milestones.

Shut down cleanly before restoring the disposable verified snapshot:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action Stop

powershell -NoProfile -ExecutionPolicy Bypass `
  -File tools/xp-vm-control.ps1 -Action RestoreSnapshot `
  -SnapshotName '<verified snapshot name>'
```

`Stop` does not hard-power-off by default. Use `-ForcePowerOff` only when the
guest shutdown has timed out and discarding the current guest state is safe.
