# -*- coding: utf-8 -*-
"""DIA (Debug Interface Access) 符號解析: RVA -> 函式名 + 行號。

純 ctypes 呼叫 VS 的 msdia140.dll (COM 介面), 零註冊、零安裝。
供 crash_report.py 把 dmp 崩潰點附上符號。

用法:
    from dia_symbol import DiaPdb
    pdb = DiaPdb(r"L:\finaldebug\QSanguosha-v2\release\QSanguosha.pdb")
    print(pdb.lookup(0xB5CFD8))   # -> (name, disp, "file:line")
"""
import ctypes

import os

from ctypes import wintypes as w

# ── GUID 常數 (取自 VS2019 DIA SDK dia2.h) ─────────────────
CLSID_DiaSource = "{E6756135-1E65-4D17-8576-610761398C3C}"
IID_IDiaDataSource = "{79F1BB5F-B66E-48E5-B6A9-1545C323CA3D}"
IID_IDiaSession = "{2F609EE1-D1C8-4E24-8288-3326BADCD211}"
IID_IDiaSymbol = "{CB787B2F-BD6C-4635-BA52-933126BD2DCD}"
IID_IClassFactory = "{00000001-0000-0000-C000-000000000046}"

# SymTagEnum
SymTagNull = 0


def guid_ptr(guid_str):
    """GUID 字串 -> (c_void_p 指標, 保留用 buffer)。bytes 含 \0, 不能走 c_char_p。"""
    buf = ctypes.create_string_buffer(guid_bytes(guid_str), 16)
    return ctypes.cast(buf, ctypes.c_void_p), buf


def guid_bytes(guid_str):
    """"{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}" -> GUID 記憶體 bytes。"""
    s = guid_str.strip("{}")
    parts = s.split("-")
    b = bytes.fromhex(parts[0])[::-1]
    b += bytes.fromhex(parts[1])[::-1]
    b += bytes.fromhex(parts[2])[::-1]
    b += bytes.fromhex(parts[3])
    b += bytes.fromhex(parts[4])
    return b


HRESULT = ctypes.c_long


class ComPtr:
    """COM 物件包裝: 以 ctypes 呼叫 vtable 方法。
    call(index, sig, *args): sig = 參數 ctypes 型別列表。
    注意: self.ptr 指向介面物件, 首 qword 才是 vtable 指標。"""

    def __init__(self, ptr):
        self.ptr = ptr

    def _slot(self, index):
        vtable_addr = ctypes.cast(
            self.ptr, ctypes.POINTER(ctypes.c_void_p))[0]
        vtbl = ctypes.cast(vtable_addr, ctypes.POINTER(ctypes.c_void_p))
        return vtbl[index]

    def call(self, index, sig, *args):
        fn = ctypes.cast(
            self._slot(index), ctypes.CFUNCTYPE(ctypes.c_long, ctypes.c_void_p, *sig))
        return fn(self.ptr, *args)

    def release(self):
        fn = ctypes.cast(self._slot(2), ctypes.CFUNCTYPE(w.ULONG, ctypes.c_void_p))
        return fn(self.ptr)


def find_msdia():
    """依序搜尋 VS 的 msdia140.dll (優先 amd64 版, 需與 Python 位元數一致)。"""
    bases = [
        r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\DIA SDK\bin\amd64",
        r"C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\Common7\IDE",
        r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE",
    ]
    for b in bases:
        p = os.path.join(b, "msdia140.dll")
        if os.path.isfile(p):
            return p
    raise FileNotFoundError("找不到 msdia140.dll (VS DIA SDK)")


class DiaPdb:
    """以 DIA 開啟 PDB, 提供 RVA -> (名稱, displacement, 檔案:行號)。"""

    def __init__(self, pdb_path, msdia_path=None):
        self.msdia = ctypes.WinDLL(msdia_path or find_msdia())
        self.session = self._open(pdb_path)

    def _open(self, pdb_path):
        DllGetClassObject = self.msdia.DllGetClassObject
        DllGetClassObject.restype = HRESULT
        DllGetClassObject.argtypes = [ctypes.c_void_p, ctypes.c_void_p,
                                      ctypes.POINTER(ctypes.c_void_p)]
        cf = ctypes.c_void_p()
        clsid_p, clsid_b = guid_ptr(CLSID_DiaSource)
        iid_cf_p, iid_cf_b = guid_ptr(IID_IClassFactory)
        hr = DllGetClassObject(clsid_p, iid_cf_p, ctypes.byref(cf))
        if hr < 0:
            raise RuntimeError("DllGetClassObject failed hr=0x%08X" % (hr & 0xFFFFFFFF))
        factory = ComPtr(cf.value)
        # IClassFactory::CreateInstance(outer, riid, ppv) = vtbl[3]
        src = ctypes.c_void_p()
        iid_ds_p, iid_ds_b = guid_ptr(IID_IDiaDataSource)
        hr = factory.call(3, [ctypes.c_void_p, ctypes.c_void_p,
                              ctypes.POINTER(ctypes.c_void_p)],
                          None, iid_ds_p, ctypes.byref(src))
        factory.release()
        if hr < 0:
            raise RuntimeError("CreateInstance failed hr=0x%08X" % (hr & 0xFFFFFFFF))
        source = ComPtr(src.value)
        # IDiaDataSource::loadDataFromPdb = vtbl[4]
        hr = source.call(4, [ctypes.c_wchar_p], ctypes.c_wchar_p(pdb_path))
        if hr < 0:
            source.release()
            raise RuntimeError("loadDataFromPdb failed hr=0x%08X" % (hr & 0xFFFFFFFF))
        # openSession = vtbl[8]
        sess = ctypes.c_void_p()
        hr = source.call(8, [ctypes.POINTER(ctypes.c_void_p)], ctypes.byref(sess))
        source.release()
        if hr < 0:
            raise RuntimeError("openSession failed hr=0x%08X" % (hr & 0xFFFFFFFF))
        return ComPtr(sess.value)

    def close(self):
        if self.session:
            self.session.release()
            self.session = None

    def lookup(self, rva):
        """RVA -> (函式名, displacement, "檔案:行號")。"""
        sym = ctypes.c_void_p()
        disp = ctypes.c_long(0)
        # IDiaSession::findSymbolByRVAEx = vtbl[19]
        hr = self.session.call(19, [w.DWORD, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_void_p),
                                    ctypes.POINTER(ctypes.c_long)],
                               rva, SymTagNull, ctypes.byref(sym),
                               ctypes.byref(disp))
        name = None
        if hr >= 0 and sym.value:
            so = ComPtr(sym.value)
            name = self._get_name(so)
            so.release()
        line = self._line_for_rva(rva)
        return name, disp.value, line

    def _get_name(self, sym):
        # IDiaSymbol::get_name = vtbl[5] (BSTR*)
        bstr = ctypes.c_void_p()
        hr = sym.call(5, [ctypes.POINTER(ctypes.c_void_p)], ctypes.byref(bstr))
        if hr < 0 or not bstr.value:
            return None
        try:
            p = ctypes.cast(bstr.value, ctypes.c_wchar_p)
            return p.value
        finally:
            ole32 = ctypes.WinDLL("oleaut32")
            ole32.SysFreeString.argtypes = [ctypes.c_void_p]
            ole32.SysFreeString(bstr.value)

    def _line_for_rva(self, rva):
        # IDiaSession::findLinesByRVA = vtbl[25]
        lines = ctypes.c_void_p()
        hr = self.session.call(25, [w.DWORD, w.DWORD,
                                    ctypes.POINTER(ctypes.c_void_p)],
                               rva, 1, ctypes.byref(lines))
        if hr < 0 or not lines.value:
            return None
        lo = ComPtr(lines.value)
        n = w.ULONG(0)
        # IDiaEnumLineNumbers::get_Count = vtbl[4]
        lo.call(4, [ctypes.POINTER(w.ULONG)], ctypes.byref(n))
        if n.value == 0:
            lo.release()
            return None
        item = ctypes.c_void_p()
        # Item = vtbl[5]
        lo.call(5, [w.ULONG, ctypes.POINTER(ctypes.c_void_p)],
                0, ctypes.byref(item))
        lo.release()
        if not item.value:
            return None
        ln = ComPtr(item.value)
        lnum = w.ULONG(0)
        # IDiaLineNumber::get_lineNumber = vtbl[5]
        ln.call(5, [ctypes.POINTER(w.ULONG)], ctypes.byref(lnum))
        fname = None
        # IDiaLineNumber::get_sourceFile = vtbl[4]
        sf = ctypes.c_void_p()
        hr = ln.call(4, [ctypes.POINTER(ctypes.c_void_p)], ctypes.byref(sf))
        if hr >= 0 and sf.value:
            so = ComPtr(sf.value)
            bstr = ctypes.c_void_p()
            # IDiaSourceFile::get_fileName = vtbl[4]
            so.call(4, [ctypes.POINTER(ctypes.c_void_p)], ctypes.byref(bstr))
            if bstr.value:
                fname = ctypes.cast(bstr.value, ctypes.c_wchar_p).value
                ole32 = ctypes.WinDLL("oleaut32")
                ole32.SysFreeString.argtypes = [ctypes.c_void_p]
                ole32.SysFreeString(bstr.value)
            so.release()
        ln.release()
        if fname is None:
            return "line %d" % lnum.value
        return "%s:%d" % (os.path.basename(fname.replace("\\\\", "\\")), lnum.value)


if __name__ == "__main__":
    import sys
    pdb = DiaPdb(sys.argv[1])
    for a in sys.argv[2:]:
        rva = int(a, 16)
        name, disp, line = pdb.lookup(rva)
        print("0x%08X  %s  disp=0x%X  %s" % (rva, name, disp, line))
    pdb.close()
