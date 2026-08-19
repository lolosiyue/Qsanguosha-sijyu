# -*- coding: utf-8 -*-
"""一鍵清理舊的遊戲記錄: dmp / record / autotest-logs / 散落 log。

用法:
    python cleanup_records.py --exe-root L:\\finaldebug\\QSanguosha-v2
    python cleanup_records.py --exe-root \\\\DESKTOP-VON1J9F\\game\\sgs\\QSanguoshaFinal
    python cleanup_records.py --dry-run            # 只預覽不刪除

刪除範圍 (目錄結構本身與 dmp/README 保留):
  <root>/dmp/*.dmp
  <root>/dmp/crash-*.txt  <root>/dmp/crash-*-config.ini  (crashhandler 報告)
  <root>/record/*.txt
  <root>/tools/autotest/autotest-logs/  (headless/network/crash-report + summary csv/png)
  <root>/client_autotest_diag.log
  <root>/headless_verify*.log  <root>/headless_log_*.txt
"""
import argparse
import fnmatch
import os
import shutil
import sys


def collect(exe_root):
    """收集待清理項目: (path, rel, is_dir)。"""
    items = []

    def add(path, is_dir=False):
        items.append((path, os.path.relpath(path, exe_root), is_dir))

    d = os.path.join(exe_root, "dmp")
    if os.path.isdir(d):
        for n in sorted(os.listdir(d)):
            if n.lower().endswith(".dmp"):
                add(os.path.join(d, n))

    # crashhandler 報告 (08-10 crashhandler 移植後新格式: txt + config.ini)
    for pat in ("crash-*.txt", "crash-*-config.ini"):
        for base in (os.path.join(exe_root, "dmp"), exe_root):
            if not os.path.isdir(base):
                continue
            for n in sorted(os.listdir(base)):
                if fnmatch.fnmatch(n, pat):
                    add(os.path.join(base, n))

    d = os.path.join(exe_root, "record")
    if os.path.isdir(d):
        for n in sorted(os.listdir(d)):
            if n.lower().endswith(".txt"):
                add(os.path.join(d, n))

    # autotest-logs: 根層 + tools/autotest 下 (兩代 runner 產出位置)
    for d in (os.path.join(exe_root, "autotest-logs"),
              os.path.join(exe_root, "tools", "autotest", "autotest-logs")):
        if os.path.isdir(d):
            for n in sorted(os.listdir(d)):
                p = os.path.join(d, n)
                add(p, os.path.isdir(p))

    p = os.path.join(exe_root, "client_autotest_diag.log")
    if os.path.isfile(p):
        add(p)

    # 根目錄與 release 散落 log
    for pat in ("headless_verify*.log", "headless_log_*.txt"):
        for base in (exe_root, os.path.join(exe_root, "release")):
            if not os.path.isdir(base):
                continue
            for n in sorted(os.listdir(base)):
                if fnmatch.fnmatch(n, pat):
                    add(os.path.join(base, n))
    return items


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(errors="replace")
    parser = argparse.ArgumentParser(
        description="一鍵清理舊的遊戲記錄 (dmp/record/autotest-logs/散落 log)")
    parser.add_argument("--exe-root", default=os.getcwd(),
                        help="遊戲根目錄 (預設: 當前目錄)")
    parser.add_argument("--dry-run", action="store_true",
                        help="只列出待清理項目，不刪除")
    args = parser.parse_args()

    exe_root = args.exe_root.rstrip("\\/")
    items = collect(exe_root)
    if not items:
        print("nothing to clean in %s" % exe_root)
        return 0

    n_files = n_dirs = 0
    for path, rel, is_dir in items:
        if args.dry_run:
            print("[dry-run] %s" % rel)
            continue
        try:
            if is_dir:
                # 只清 autotest-logs 內子目錄 (collect 產生的路徑天然受限於 exe_root)
                shutil.rmtree(path)
                n_dirs += 1
            else:
                os.remove(path)
                n_files += 1
        except OSError as e:
            # 檔案被 exe 鎖定 (例如 record/debug.txt) 時跳過，不中斷
            print("skip %s (%s)" % (rel, e))

    if not args.dry_run:
        print("removed %d file(s), %d dir(s) from %s"
              % (n_files, n_dirs, exe_root))
    return 0


if __name__ == "__main__":
    sys.exit(main())
