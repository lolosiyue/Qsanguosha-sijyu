"""Minimal ELF reader for the Linux packaging tools.

Only what packaging needs: DT_NEEDED, DT_SONAME, DT_RPATH and DT_RUNPATH.
Implemented against the ELF64 spec rather than shelling out, so the packaging
step and the RPATH audit behave identically wherever they run and never depend
on `readelf`/`ldd` being installed or on the host's library search order.
"""

from __future__ import annotations

import pathlib
import struct
from dataclasses import dataclass, field

ELF_MAGIC = b"\x7fELF"
ELFCLASS64 = 2
PT_DYNAMIC = 2
DT_NULL = 0
DT_NEEDED = 1
DT_SONAME = 14
DT_RPATH = 15
DT_RUNPATH = 29
DT_STRTAB = 5
DT_STRSZ = 10


@dataclass
class ElfInfo:
    path: pathlib.Path
    soname: str | None = None
    needed: list[str] = field(default_factory=list)
    rpath: list[str] = field(default_factory=list)
    runpath: list[str] = field(default_factory=list)

    @property
    def search_paths(self) -> list[str]:
        """DT_RUNPATH wins over DT_RPATH when both are present."""
        return self.runpath or self.rpath


class NotAnElf(Exception):
    pass


def _cstring(blob: bytes, offset: int) -> str:
    end = blob.index(b"\x00", offset)
    return blob[offset:end].decode("utf-8", "replace")


def read(path: pathlib.Path) -> ElfInfo:
    """Parse one ELF64 little-endian shared object or executable."""
    data = pathlib.Path(path).read_bytes()
    if len(data) < 64 or data[:4] != ELF_MAGIC:
        raise NotAnElf(f"{path} is not an ELF file")
    if data[4] != ELFCLASS64:
        raise NotAnElf(f"{path} is not ELF64")
    if data[5] != 1:
        raise NotAnElf(f"{path} is not little-endian")

    e_phoff, = struct.unpack_from("<Q", data, 0x20)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)

    # Program headers give both the dynamic segment and the vaddr -> file
    # offset mapping needed to find the dynamic string table.
    loads: list[tuple[int, int, int]] = []  # (vaddr, filesz, offset)
    dynamic: tuple[int, int] | None = None
    for index in range(e_phnum):
        base = e_phoff + index * e_phentsize
        p_type, = struct.unpack_from("<I", data, base)
        p_offset, p_vaddr = struct.unpack_from("<QQ", data, base + 0x08)
        p_filesz, = struct.unpack_from("<Q", data, base + 0x20)
        if p_type == 1:  # PT_LOAD
            loads.append((p_vaddr, p_filesz, p_offset))
        elif p_type == PT_DYNAMIC:
            dynamic = (p_offset, p_filesz)

    info = ElfInfo(path=pathlib.Path(path))
    if dynamic is None:
        return info

    def to_offset(vaddr: int) -> int | None:
        for start, size, offset in loads:
            if start <= vaddr < start + size:
                return offset + (vaddr - start)
        return None

    entries: list[tuple[int, int]] = []
    offset, size = dynamic
    for position in range(offset, offset + size, 16):
        tag, value = struct.unpack_from("<qQ", data, position)
        if tag == DT_NULL:
            break
        entries.append((tag, value))

    strtab_vaddr = next((value for tag, value in entries if tag == DT_STRTAB), None)
    if strtab_vaddr is None:
        return info
    strtab_offset = to_offset(strtab_vaddr)
    if strtab_offset is None:
        return info
    strsz = next((value for tag, value in entries if tag == DT_STRSZ), 0)
    strtab = data[strtab_offset:strtab_offset + strsz] if strsz else data[strtab_offset:]

    for tag, value in entries:
        if tag == DT_NEEDED:
            info.needed.append(_cstring(strtab, value))
        elif tag == DT_SONAME:
            info.soname = _cstring(strtab, value)
        elif tag == DT_RPATH:
            info.rpath = [part for part in _cstring(strtab, value).split(":") if part]
        elif tag == DT_RUNPATH:
            info.runpath = [part for part in _cstring(strtab, value).split(":") if part]
    return info


def is_elf(path: pathlib.Path) -> bool:
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == ELF_MAGIC
    except OSError:
        return False


def expand_origin(entry: str, origin: pathlib.Path) -> str:
    """Resolve $ORIGIN / ${ORIGIN} in one RPATH entry."""
    return entry.replace("${ORIGIN}", str(origin)).replace("$ORIGIN", str(origin))
