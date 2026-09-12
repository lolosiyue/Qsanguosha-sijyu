# Windows XP SP3 / Win7 x86 legacy build

## Support boundary

| Item | XP legacy product |
|---|---|
| OS / architecture | Windows XP SP3 x86 and Windows 7 x86 (same PE32 binary and portable payload) |
| Supported room size | 2–10 players |
| 20-player room | Connection may be attempted; compatibility is not promised |
| Executables | Paired `QSanguoshaXP.exe` GUI and `QSanguoshaXPServer.exe` dedicated helper; `QSanguoshaXP.exe -server` remains the compatibility launcher |
| UI | Existing `QGraphicsScene` / `StartScene` classic UI |
| Effects | Forced to `NONE`; the setting is hidden |
| Audio | x86 FMOD Ex 4.44.53 in both Debug and Release |
| Excluded features | QML, Spine, video, OpenGL, and the WebSocket gateway |
| Runtime content | Full local `lua/`, `lua/ai/`, and `extensions/` snapshot from `-AssetRoot` |
| Listen path | Native TCP `9527` only. Compact web / port `9528` is not part of this product |
| Distribution | Portable folder or Joliet ISO with `INSTALL.CMD` |

This is an opt-in legacy product. The normal `debug` target remains the Qt 6.11
x64 development build and does not inherit the XP toolchain or feature cuts.
The portable XP payload contains both paired executables: the GUI starts the
dedicated helper for local hosting, while `QSanguoshaXP.exe -server` forwards
to that helper for compatibility with the historical command line.

There is no separate Win7 build tier. The `v141_xp` / Qt 5.6.3 x86 artifact is
the only legacy deliverable; Win7 x86 is covered by upward compatibility of the
same portable folder or ISO `PAYLOAD/` tree.

## Toolchain and Qt baseline

- Visual Studio 2017 Build Tools 15.9, MSVC 14.16
- `v141_xp`, Win32
- Windows SDK 7.1A system libraries plus the v141 Universal CRT
- Official Qt 5.6.3 MSVC 2015 x86 development/runtime tree
- `/Zc:threadSafeInit-` for the XP target

`legacy/xp/tools/build-xp.ps1` is the only supported XP build/deploy entry point.
The `xp-vs2017-x86`, `xp-debug`, `xp-release` and `xp-deploy-*` CMake presets are
implementation details used by that script, not a second public workflow.

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
the local runtime assets, including the complete `extensions/` directory. It
removes QML/video files plus repository and synchronization metadata. Record
the source SHA and deployed extension hashes with acceptance evidence because
the external extension repository can change independently.
`xp-payload-manifest.json` is the single completion and integrity manifest: it
binds the paired executables, build identity and deployed runtime files. Do not
restore the obsolete split `xp-build-identity.txt` or `xp-executables.sha256`
artifacts.
FMOD binaries must come from a distribution source whose licence has been
approved; they are not committed by this branch.

The compatibility server launcher is:

```powershell
QSanguoshaXP.exe -server
```

The command forwards to the paired `QSanguoshaXPServer.exe`; do not remove the
helper from the portable folder or ISO. The GUI's local hosting, private games
and replay takeover also start that helper directly through the local server
controller.
Managed helper stdout and stderr are retained under the writable user data root
as `logs/xp-server-<session>.stdout.log` and
`logs/xp-server-<session>.stderr.log`. They are diagnostic artifacts only and
do not determine helper readiness.

## GUI/helper process boundary

The GUI/helper process split is implemented; the merged design notes below were
absorbed from the retired `windows-xp-process-split.md` (removed 2026-09-12).
Its old acceptance-record table is superseded by the evidence section of this
document.

The private control transport is QLocalServer/QLocalSocket, unrelated to
gameplay TCP/Protocol V2. Frames have a four-byte big-endian length followed by
a JSON object. Maximum payload is 256 KiB; input and queued output are bounded.
All messages carry version, session, generation and decimal-string request ID.
Seeds and generation are decimal strings. JSON numeric fields accept only
finite integral values in range.

The GUI listens with `UserAccessOption` and a fresh CryptoAPI random token for
each launch. The helper receives the token through its environment, removes it
immediately, and authenticates before receiving initialization. Tokens never
enter ordinary logs, game packets or command lines. This prevents
accidental/cross-user attachment; it does not defend against a debugger running
with the same Windows identity.

States: `Idle -> Launching -> Handshaking -> Initializing -> Ready -> Stopping
-> Idle`. Failure also stops the owned process before permitting reuse. Every
callback is tied to its QProcess and generation. External/standalone processes
are never adopted. Startup, authentication, requests, takeover and shutdown
have explicit deadlines. QProcess owns the process handle. A helper also opens
and verifies its parent's creation time before engine initialization and
retains that handle: PID reuse cannot attach it to another process. A native
wait thread observes parent death even during Lua initialization; this
emergency exit is forced, never graceful. Normal control loss requests the
existing server/room/engine cleanup. A shutdown timeout kills only the owned
QProcess and records the incomplete phase.

GUI entries that start a managed helper, and the checks each requires:

| Entry | Split behavior | Required check |
|---|---|---|
| ServerDialog, join locally | OwnedHost helper, ready then native TCP signup | owner, AI, selected settings, port conflict |
| ServerDialog, server only | OwnedHost, management events and discovery in helper | management, another client, no second helper |
| startLocalConsoleGame | OwnedPrivate helper, loopback port 0 | cancel, retry, AI game |
| complete/failLocalRoomStart | authenticated ready/error/exit | no premature connection |
| startTakeoverGame | OwnedPrivate with snapshot/replay/manifest hashes | live takeover readiness |
| rollbackTakeover | stop owned helper, retain replay restore state | position, perspective, pause |
| startConnection/reconnect/restart | same Client, reuse ready owned endpoint | no new helper, external isolation |
| showHomePage/gotoStartScene | stop private/joined host; retain host-only management | no orphan, repeat start |
| closeEvent | asynchronous shutdown then exit notification | initialization, game, takeover, timeout |
| BroadcastBox | bounded broadcast command with reply | delivery/error |
| BanIpDialog | player snapshot, kick/ban command, owner persists ack | offline player, stale result |
| StartScene | controller log/status signals | bounded log, endpoint |
| -connect | unchanged, never owns remote server | remote process remains alive |
| -server | early QCore forwarding to standalone helper | args, exit code, missing helper |
| QSanguoshaXPServer | standalone dedicated CLI or managed helper | no GUI/audio modules |

The GUI captures a session-local INI before launch; all primitive persisted
values, lists and extension keys plus active game settings are preserved. The
helper's Settings object is bound to that INI before static initialization, and
overrides are installed before EngineBootstrap. The GUI owns persistent
settings. Ban changes are persisted only after a correlated successful reply.
Assets are shared read-only; runtime data, logs, replay/snapshot pairs and
diagnostics use the user's writable data root.

Ready means validated settings, paired source build, matching rules manifest,
initialized runtime, prepared initial room and a successfully bound native
socket. Private sessions bind 127.0.0.1:0 in the helper; the actual socket
remains bound. Host sessions use the configured endpoint and
discovery/listing behavior. Takeover_ready is distinct from ready and commits
rollback data only after the existing `Server::takeoverReady` signal.

### 20-player memory acceptance methodology (pending)

Memory acceptance must be frozen after measuring the original payload and
before measuring the new payload. Record private bytes, working set,
VirtualQueryEx address map (committed/reserved/free and largest free region),
handles, threads, system commit and stage markers at one-second intervals, with
identical assets, seed, mode and player count. Never substitute working set for
address-space pressure. Report both maximum per-process pressure and combined
private bytes. No threshold or PASS is claimed before that baseline exists.
20 players remains exploratory.

## ISO media

Create the XP-compatible ISO from a completed portable Release folder:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass `
  -File legacy/xp/tools/new-xp-iso.ps1 `
  -SourceDirectory C:\out\QSanguoshaXP-release `
  -IsoPath C:\out\QSanguosha-XP-SP3-x86.iso `
  -VolumeLabel QSAN_XP
```

The ISO keeps the portable tree under `PAYLOAD/` and must contain both paired
executables. `INSTALL.CMD` first leaves
the target directory, then performs a clean `xcopy /E` installation to
`C:\QSanguoshaXP`. `RUNXP.CMD` is only the acceptance wrapper; it reuses
`INSTALL.CMD` and requests the acceptance launch marker instead of duplicating
the installation and launch logic. CAB extraction is intentionally not used because XP
`expand.exe` flattens destination subdirectories. `-ReuseStage` may be used
when only `AUTORUN.INF`, `INSTALL.CMD` or `RUNXP.CMD` changed.

## Win7 x86 VM acceptance (same portable payload)

Win7 x86 validation reuses the exact portable Release folder produced by
`-Deploy` (or the ISO `PAYLOAD/` extracted from that folder). Do not build,
deploy, or gate a second Win7-specific payload.

Recommended guest checks on Windows 7 x86 SP1:

1. copy or install the same portable tree used for XP acceptance;
2. launch `QSanguoshaXP.exe` and confirm the classic `StartScene` main window;
3. start the compatibility launcher (`QSanguoshaXP.exe -server`) on the guest
   and connect with `-connect:127.0.0.1`;
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
3. the paired GUI/helper started a local server and a client using
   `-connect:127.0.0.1` entered the waiting room;
4. Qt 5.6.3, QtNetwork and FMOD loaded without an XP loader failure.

The 2026-09-03 rerun used `debug@c622ec7f` plus the current local runtime tree.
The deployed and guest-recovered `QSanguoshaXP.exe` both had SHA-256
`CB35818991831458FACF1684BB1FBDBA7B8067471852C70ED4D13E8220A925AD`.

| Gate | Result |
|---|---|
| Win32 Release build, PE 5.01 and import checks | PASS |
| Full local `extensions/` deployment | PASS: 106/106 files, SHA-256 differences 0 |
| Local TCP 9527 connection and `AutoAddRobots` | PASS |
| General selection | Progressed with a valid offered general |
| Actual `GAME_STARTED` marker within 60 seconds | NOT OBSERVED |
| `GAME_OVER` | NOT OBSERVED |
| Guest and host VirtualBox process cleanup | PASS |

The server repeatedly failed to create the per-game snapshot directory under
the XP `NetworkService` profile after general selection. This is the next
diagnostic lead, not yet a proven cause of the missing game start. Evidence is
under `builds/xp-poc/xp-logs-20260903-debug-c622ec7-runtime/`. This run is not a
complete gameplay acceptance pass.

A same-day follow-up isolated the actual blocker: Qt 5.6 converts JSON numbers
to `QVariant::Double`, while `PlayerUIState::tryParseInt()` only accepted the
integer QVariant metatypes. The client therefore rejected an otherwise valid
`PlayerUiStatePayload` before `Client::startGame()`. The parser now accepts only
finite, exactly integral doubles within the `int` range; fractional and
out-of-range values remain invalid. A focused protocol executable covers both
the Qt 5 representation and the rejection case.

The rebuilt `debug@c622ec7f` package and the guest-recovered executable both had
SHA-256
`F173B5E5441E9CD91443C08B5A9EDCA7DAE7430C2110C7BB46FD575711A692DC`.
The follow-up used the complete local runtime and all 106 `extensions/` files
in `03_1v2`, seed `20260903`, with zero AI delay.

| Follow-up gate | Result |
|---|---|
| Win32 Release build, PE 5.01 and import checks | PASS |
| Source/package/guest `extensions/` equality | PASS: 106/106 files, SHA-256 differences 0 |
| Local TCP 9527 connection and `AutoAddRobots` | PASS |
| `GAME_STARTED` | PASS: 22:47:13.388, 3 players |
| `GAME_OVER` | PASS: 22:48:46.532, client defeat, 93.144 seconds after start |
| Server final lifetime gauge | PASS: `CARD_LIFETIME_ZERO` |
| Guest/host process cleanup | PASS: no running VM, `VBoxHeadless`, or `VirtualBoxVM`; VM state is `aborted` after the authorized forced host-process stop |

Evidence is under
`builds/xp-poc/xp-logs-20260903-fullgame-c622ec7-runtime/final/`. The snapshot
directory warning still repeats on XP, but the completed match proves that it
is not the game-start blocker. It remains a separate diagnostic item.

VirtualBox 7.0.2 on the host crashed when VM audio was enabled, so the VM used
`audio=none`. FMOD load/initialization is covered, but audible playback remains
a physical/alternate-hypervisor acceptance gate. Rooms above 10 players and
20-player memory/load behavior remain outside the compatibility commitment.
Packaging all local extensions proves deployment completeness; individual
third-party extension behavior still requires gameplay coverage for the exact
deployed snapshot.

The split-process Replay takeover/rollback, reconnect and management GUI flows
have not yet completed XP guest acceptance. Until those flows are exercised on
the designated VM, a missing PASS is an open acceptance gate rather than a
confirmed product defect. The opt-in GUI acceptance driver now waits for room
ownership after `roomSceneCreated`, so slow helper initialization cannot consume
the automatic AI-fill window before that scene exists.

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

`CopyFromGuest` recursively accepts a guest file or directory and deliberately
requires an existing host destination directory; passing a nonexistent filename
as the destination causes VirtualBox `VERR_FILE_NOT_FOUND`.

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
