# XP process probe preparation record

The original preparation record did not identify a date or source commit.

Initial verification: v141_xp `/W4` build succeeded; `dumpbin` reports x86,
OS/subsystem 5.01, and only PSAPI.DLL/KERNEL32.dll imports. PSAPI functions use
their XP names, without `K32*` imports. `--help` and an out-of-range sample count
were checked. No target memory sampling or guest execution was performed during
tool preparation. PE/import checks do not establish guest runtime acceptance.


[Probe usage](../xp-process-probe.md)
