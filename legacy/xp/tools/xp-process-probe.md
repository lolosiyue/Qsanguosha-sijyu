# XP native process probe

`xp-process-probe.cpp` is a read-only Win32 console tool for **XP SP3 x86**.
It does not require Qt, install hooks, inject code, alter target processes, or
capture screenshots. It opens targets with `PROCESS_QUERY_INFORMATION`,
`PROCESS_VM_READ`, and (when sampling) `SYNCHRONIZE`; it never requests write or
termination rights. The CRT may import `TerminateProcess` for its own fatal
error handling; the probe has no target-termination call.

```bat
xp-process-probe.exe observe --name QSanguoshaXP.exe
xp-process-probe.exe observe --name QSanguoshaXPServer.exe
xp-process-probe.exe --pid 1234 --samples 10 --interval-ms 1000 > gui.jsonl
```

`observe` lists case-insensitive exact executable-name matches and their full
loaded image paths, including an explicit error for inaccessible paths. It
finishes with an `observe_complete` match count and takes no memory samples.
Sampling retains the original process handle and records its creation FILETIME;
it never reopens a possibly reused PID. An exited process ends observation.
The x86 probe is intended for x86 targets; use it inside the XP guest rather than
to inspect an arbitrary x64 host process.

Output is UTF-8 JSONL on stdout. The first sampling iteration emits actual
loaded module paths from `EnumProcessModules` / `GetModuleFileNameExW`, then a
`sample` record. Modules can load or unload during enumeration; enumeration
failure is explicit and retry count is bounded. No module list is inferred from
deployment folder contents. Each sample includes its image path and UTC start
and finish timestamps. Windows XP clock precision and wall-clock adjustments
apply. Calls within a sample are sequential, not an atomic snapshot.

| Field | Definition and interpretation |
|---|---|
| `process_created_filetime` | Original process creation time, unsigned 100 ns intervals since 1601-01-01 UTC; retain losslessly when parsing JSON numbers. |
| `memory.private_bytes` | `PROCESS_MEMORY_COUNTERS_EX.PrivateUsage`: private committed memory, not resident RAM. |
| `memory.working_set_bytes` | Currently resident working set, including shared pages. |
| `memory.peak_working_set_bytes` | Process-lifetime peak working set; not reset at the start of observation. |
| `memory.pagefile_bytes`, `peak_pagefile_bytes` | Raw `PagefileUsage` / `PeakPagefileUsage` commit-accounting counters; do not interpret as bytes physically written to the page file. |
| `virtual.committed_bytes` | Sum of `MEM_COMMIT` regions from `VirtualQueryEx`, including mapped/shared regions. Distinct from private bytes and working set. |
| `virtual.reserved_bytes` | Sum of `MEM_RESERVE` regions. Committed pages within allocations are counted separately. |
| `virtual.free_bytes` | Sum of `MEM_FREE` regions in the reported scan range. |
| `virtual.largest_free_bytes` | Largest individual free region in that range; indicates contiguous virtual-address headroom, not physical memory. |
| `virtual.range_begin`, `range_end_exclusive` | Address range obtained from the probe's `GetSystemInfo` minimum and maximum application addresses. No hardcoded 2 GiB limit. |
| `virtual.complete`, `scanned_end_exclusive` | Whether the entire reported range was read. Totals are partial when `complete` is false; retain the accompanying Win32 error. |
| `handles` | `GetProcessHandleCount`; operating-system handles, not a count of Qt objects. |
| `threads` | Toolhelp system thread-snapshot entries owned by the sampled PID. |
| `system_commit.total_bytes`, `limit_bytes`, `peak_bytes` | `GetPerformanceInfo` commit page counters multiplied by its page size; system-wide, not attributable exclusively to the target. |

Failures use `null` plus a Win32 error for unavailable counters, or an `error`
record for operation failures. A sample can contain partial failures even when
the probe exits successfully: analysis must inspect these fields. Exit code 2
means invalid arguments; code 1 means an opening/enumeration/wait failure.
No matching process is a successful `observe` with zero matches.

Defaults are one sample and a 1000 ms interval. Limits are 3600 samples,
10–60000 ms between iterations, and a scheduled wait span of at most one hour.
The interval is a delay after the previous sample, so scan work adds elapsed
time. The retained-handle wait ends early when the process exits.

## Standalone build and verification

Keep this utility outside the product targets. Use VS 2017 **v141_xp**, Win32,
Windows SDK 7.1A Win32 headers/libraries and the repository's installed
**10.0.17763.0 UCRT** headers/static libraries. Select `/MT`, `/utf-8`,
`/Zc:threadSafeInit-`, `/SUBSYSTEM:CONSOLE,5.01`, `/OSVERSION:5.1`, and `psapi.lib`.
The source defines `PSAPI_VERSION=1`, `WINVER=0x0501`, and `_WIN32_WINNT=0x0501`.
Do not compile against the default modern toolset and only rewrite the PE header.

An isolated local build project is generated at
`builds/xp-process-probe/xp-process-probe.vcxproj`; its executable is
`builds/xp-process-probe/bin/xp-process-probe.exe`. Rebuild that project using:

```powershell
& 'C:/Program Files (x86)/Microsoft Visual Studio/2017/BuildTools/MSBuild/15.0/Bin/MSBuild.exe' `
    builds/xp-process-probe/xp-process-probe.vcxproj `
    /p:Configuration=Release /p:Platform=Win32 /v:minimal /nologo
```

The generated project/build directory is a local artifact, not a tracked
project dependency. On a new checkout, create a standalone console project
using the configuration above and include only `xp-process-probe.cpp`.
The UCRT include/library search paths may need explicit configuration because
the v141_xp SDK selection does not always populate them automatically.

Initial verification: v141_xp `/W4` build succeeded; `dumpbin` reports x86,
OS/subsystem 5.01, and only PSAPI.DLL/KERNEL32.dll imports. PSAPI functions use
their XP names, without `K32*` imports. `--help` and an out-of-range sample count
were checked. No target memory sampling or guest execution was performed during
tool preparation. PE/import checks do not establish guest runtime acceptance.

Establish the required unchanged baseline first, then collect measurements under
the agreed test scenario. Do not label host measurements as XP guest results.
