# -*- coding: utf-8 -*-
"""Minidump (breakpad) 結構化解析: 例外碼 / 位址 / 崩潰模組 / 執行緒。

供 crash_report.py 集中閃退分析使用; 純標準函式庫, 不需 cdb/windbg。
格式參考 Microsoft MINIDUMP 結構 (minidumpapiset.h)。
"""
import datetime
import os
import struct

# 例外碼翻譯表
EXC_NAMES = {
    0xC0000005: "ACCESS_VIOLATION (存取違規)",
    0xC0000409: "FAIL_FAST (fail-fast / GS cookie)",
    0xC00000FD: "STACK_OVERFLOW (堆疊溢位)",
    0xC0000025: "NONCONTINUABLE_EXCEPTION",
    0x80000003: "BREAKPOINT",
    0xC0000374: "HEAP_CORRUPTION",
    0xC0000094: "INT_DIVIDE_BY_ZERO",
    0xC000001D: "ILLEGAL_INSTRUCTION",
    0xC0000008: "INVALID_HANDLE",
    0xE06D7363: "CPP_EXCEPTION (C++ 例外)",
}

STREAM_EXCEPTION = 6
STREAM_SYSTEMINFO = 7
STREAM_MODULELIST = 4
STREAM_THREADLIST = 3
STREAM_MEMORYLIST = 5


def parse_dump(path):
    """解析 minidump, 回傳 dict:
    file / timestamp / exc_code / exc_name / exc_addr / exc_thread /
    fault_module / fault_rva / n_threads / n_modules / modules[:10]"""
    with open(path, "rb") as f:
        data = f.read()
    r = {"file": os.path.basename(path)}
    if len(data) < 32 or data[:4] != b"MDMP":
        r["error"] = "不是 minidump (缺 MDMP 簽名)"
        return r
    nstreams, dir_rva = struct.unpack_from("<II", data, 8)
    ts = struct.unpack_from("<I", data, 16)[0]
    r["timestamp"] = datetime.datetime.utcfromtimestamp(ts) \
        .strftime("%Y-%m-%d %H:%M:%S UTC")
    streams = {}
    for i in range(nstreams):
        stype, dsize, rva = struct.unpack_from("<III", data, dir_rva + i * 12)
        streams[stype] = (rva, dsize)

    if STREAM_EXCEPTION in streams:
        rva, dsize = streams[STREAM_EXCEPTION]
        tid = struct.unpack_from("<I", data, rva)[0]
        code = struct.unpack_from("<I", data, rva + 8)[0]
        addr = struct.unpack_from("<Q", data, rva + 24)[0]
        r["exc_thread"] = tid
        r["exc_code"] = "0x%08X" % code
        r["exc_name"] = EXC_NAMES.get(code, "UNKNOWN")
        r["exc_addr"] = "0x%X" % addr
    else:
        r["exc_code"] = "-"
        r["exc_name"] = "無例外串流 (可能非崩潰 dump)"

    mods = []
    if STREAM_MODULELIST in streams:
        rva, dsize = streams[STREAM_MODULELIST]
        n = struct.unpack_from("<I", data, rva)[0]
        for i in range(min(n, 1024)):
            e = rva + 4 + i * 108
            base, size = struct.unpack_from("<QI", data, e)
            name_rva = struct.unpack_from("<I", data, e + 20)[0]
            nlen = struct.unpack_from("<I", data, name_rva)[0]
            name = data[name_rva + 4:name_rva + 4 + nlen] \
                .decode("utf-16le", errors="replace")
            mods.append((base, size, os.path.basename(name)))
        r["modules"] = mods
        if r.get("exc_addr"):
            a = int(r["exc_addr"], 16)
            if a:
                hit = [(m[2], a - m[0]) for m in mods
                       if m[0] <= a < m[0] + m[1]]
                if hit:
                    r["fault_module"], r["fault_rva"] = hit[0]
    r.setdefault("fault_module", "-")
    r.setdefault("fault_rva", -1)

    if STREAM_THREADLIST in streams:
        rva, dsize = streams[STREAM_THREADLIST]
        n = struct.unpack_from("<I", data, rva)[0]
        r["n_threads"] = n

    r["n_modules"] = len(mods)
    r["modules_preview"] = [m[2] for m in mods[:8]]
    return r
