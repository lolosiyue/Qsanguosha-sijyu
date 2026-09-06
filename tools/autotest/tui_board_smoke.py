#!/usr/bin/env python3
"""pty smoke test for qsanguosha_tui board mode.

Unit tests cannot get ``TuiTerminal::enter()`` past its ``isatty()`` gate, so
raw mode, ``SIGWINCH`` and terminal restoration on exit have no coverage
without a real controlling terminal. This script opens one with
``os.forkpty()``, runs the production ``qsanguosha_tui --ui board`` against a
real ``qsanguosha_server``, and checks:

1. Entering board mode switches to the alternate screen.
2. A resize (``TIOCSWINSZ`` + ``SIGWINCH``) repaints: the client must emit a
   fresh full frame, which starts by homing the cursor.
3. ``SIGINT`` restores the terminal: alternate screen left, cursor shown.
4. The client exits 0 -- a graceful shutdown, not a kill.
5. The SAME SIGINT that restored the terminal also disconnected gracefully.
   ``TuiTerminal::enter()`` and ``tuiInstallInterruptHandler()`` share one
   SIGINT handler (see ``src/tui/tui-terminal.cpp``); the unit test can only
   prove the installer is idempotent, not that a real SIGINT under a real
   terminal does both jobs at once. This is that proof.

This is a LOCAL GATE and must never run in CI: CI runners have no stable pty,
and adding this there would only produce intermittent red (see
docs/tui-board-ui.md §7.3 / §7.5). Process discipline (own process group,
SIGTERM before a forced kill, no orphans, port released) mirrors
``tools/autotest/tui_network_smoke.py``.

Usage::

    ./debug/qsanguosha_server --port 16013 --game-mode 02p --seed 2020 &
    python3 tools/autotest/tui_board_smoke.py --exe-root . --port 16013
"""

from __future__ import annotations

import argparse
import fcntl
import os
import select
import signal
import struct
import sys
import termios
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from runner_common import (  # noqa: E402
    close_proc,
    find_exe,
    free_tcp_port,
    process_group_alive,
    resolve_workdir,
    spawn,
    terminate_tree,
    wait_port,
    wait_port_released,
)

ALT_SCREEN_ENTER = b"\x1b[?1049h"
ALT_SCREEN_LEAVE = b"\x1b[?1049l"
CURSOR_HOME = b"\x1b[H"
CURSOR_SHOW = b"\x1b[?25h"


def spawn_pty(cmd, cwd):
    """Fork cmd onto a fresh pty, making the child a session leader with that
    pty as its controlling terminal (``os.forkpty()`` does the ``setsid()`` +
    ``TIOCSCTTY`` dance for us -- a plain ``subprocess.Popen`` dup2'd onto a
    pty fd does not acquire a controlling terminal, since dup2 is not open()).

    Returns (pid, master_fd)."""
    pid, master_fd = os.forkpty()
    if pid == 0:
        try:
            os.chdir(cwd)
            os.execvp(cmd[0], cmd)
        except Exception:  # noqa: BLE001 - child path, must not return
            pass
        os._exit(127)
    return pid, master_fd


def set_winsize(fd, rows, cols):
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))


def read_until(master_fd, patterns, timeout, buf, start=0):
    """Read from master_fd, appending every byte to buf, until one of
    patterns appears in buf[start:] or timeout elapses. Returns True if found.

    The search is deliberately confined to buf[start:] rather than the whole
    cumulative buffer: a caller checking "did event X happen after point P"
    (e.g. a repaint after a specific resize) must not get a stale match from
    bytes that arrived long before P -- that would return instantly, having
    given the client no time whatsoever to react to whatever just triggered
    the wait, and silently turn "still waiting" into a false positive."""
    def found():
        return any(p in buf[start:] for p in patterns)

    if found():
        return True
    deadline = time.time() + timeout
    while True:
        remaining = deadline - time.time()
        if remaining <= 0:
            break
        ready, _, _ = select.select([master_fd], [], [], min(remaining, 0.2))
        if master_fd not in ready:
            continue
        try:
            chunk = os.read(master_fd, 65536)
        except OSError:
            break  # Linux signals pty EOF (slave side all closed) as EIO
        if not chunk:
            break
        buf.extend(chunk)
        if found():
            return True
    return found()


def waitpid_timeout(pid, timeout):
    """Poll waitpid(WNOHANG) up to timeout. Returns (exited, status)."""
    deadline = time.time() + timeout
    while True:
        try:
            wpid, status = os.waitpid(pid, os.WNOHANG)
        except ChildProcessError:
            return True, None  # already reaped elsewhere: treat as exited
        if wpid == pid:
            return True, status
        if time.time() >= deadline:
            return False, None
        time.sleep(0.05)


def cleanup_client(pid, problems):
    """Best-effort teardown if the client under test did not exit on its
    own. pid is also its own process group leader (os.forkpty() calls
    setsid()), so a single signal reaches the whole (one-process) tree."""
    if pid <= 0:
        return
    exited, _ = waitpid_timeout(pid, 0)
    if exited:
        return
    try:
        os.killpg(pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        pass
    exited, _ = waitpid_timeout(pid, 5)
    if exited:
        problems.append("client needed SIGTERM to exit (did not exit from SIGINT alone)")
        return
    try:
        os.killpg(pid, signal.SIGKILL)
    except (ProcessLookupError, PermissionError):
        pass
    waitpid_timeout(pid, 5)
    problems.append("client needed SIGKILL to exit (unresponsive to SIGTERM)")


def client_group_alive(pid):
    if pid <= 0:
        return False
    try:
        os.killpg(pid, 0)
        return True
    except (ProcessLookupError, PermissionError, OSError):
        return False


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe-root", default=".")
    parser.add_argument("--server")
    parser.add_argument("--tui")
    parser.add_argument("--workdir")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=0, help="0 picks a free ephemeral port")
    parser.add_argument("--game-mode", default="02p")
    parser.add_argument("--seed", type=int, default=2020)
    parser.add_argument("--artifact-dir", default="ci-logs/tui-board-smoke")
    parser.add_argument("--server-startup-timeout", type=float, default=30.0)
    parser.add_argument("--alt-screen-timeout", type=float, default=15.0)
    parser.add_argument("--resize-timeout", type=float, default=5.0)
    parser.add_argument("--interrupt-timeout", type=float, default=5.0)
    return parser.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)
    exe_root = os.path.abspath(args.exe_root)
    artifact_dir = os.path.abspath(args.artifact_dir)
    os.makedirs(artifact_dir, exist_ok=True)
    server_exe = (os.path.abspath(args.server) if args.server
                  else find_exe(exe_root, "qsanguosha_server"))
    tui_exe = os.path.abspath(args.tui) if args.tui else find_exe(exe_root, "qsanguosha_tui")
    workdir = os.path.abspath(args.workdir) if args.workdir else resolve_workdir(exe_root)

    port = args.port or free_tcp_port()
    server_log = os.path.join(artifact_dir, "server.log")
    transcript_path = os.path.join(artifact_dir, "client-pty.log")
    for path in (server_log, transcript_path):
        if os.path.isfile(path):
            os.remove(path)

    problems = []
    results = {
        "alt_screen_enter": False,
        "resize_repaint": False,
        "sigint_restores_terminal": False,
        "clean_exit_code": None,
        "single_sigint_shared_handler": False,
    }
    buf = bytearray()

    server_cmd = [
        server_exe,
        "--port", str(port),
        "--websocket-port", "0",
        "--bind-address", args.host,
        "--game-mode", args.game_mode,
        "--seed", str(args.seed),
    ]
    server = spawn(server_cmd, workdir, server_log)
    client_pid = -1
    master_fd = -1
    try:
        if not wait_port(port, args.server_startup_timeout, proc=server):
            problems.append("server did not listen on port %d" % port)
        else:
            tui_cmd = [
                tui_exe,
                "--host", args.host,
                "--port", str(port),
                "--name", "PtySmoke",
                "--avatar", "caocao",
                "--ui", "board",
                "--asset-root", workdir,
            ]
            client_pid, master_fd = spawn_pty(tui_cmd, workdir)
            set_winsize(master_fd, 30, 100)

            # 1. Entering board mode switches to the alternate screen.
            results["alt_screen_enter"] = read_until(
                master_fd, [ALT_SCREEN_ENTER], args.alt_screen_timeout, buf)
            if not results["alt_screen_enter"]:
                problems.append("never saw alternate-screen enter sequence %r"
                                 % ALT_SCREEN_ENTER)

            # 2. Resize repaints rather than tearing: after TIOCSWINSZ +
            #    SIGWINCH the client must emit a full frame, which starts by
            #    homing the cursor. Only bytes written *after* the resize
            #    count -- the initial connect/waiting-room draw already put
            #    at least one cursor-home in buf.
            before = len(buf)
            set_winsize(master_fd, 40, 120)
            os.kill(client_pid, signal.SIGWINCH)
            read_until(master_fd, [CURSOR_HOME], args.resize_timeout, buf, start=before)
            results["resize_repaint"] = CURSOR_HOME in bytes(buf[before:])
            if not results["resize_repaint"]:
                problems.append("no cursor-home full frame after SIGWINCH")

            # 3 & 5. One SIGINT must both restore the terminal and disconnect
            #    gracefully -- the assertion Task 5 left for a real pty to
            #    prove, since a unit test cannot get enter() past isatty().
            tail_start = len(buf)
            os.kill(client_pid, signal.SIGINT)
            read_until(master_fd, [ALT_SCREEN_LEAVE, CURSOR_SHOW], args.interrupt_timeout, buf,
                       start=tail_start)
            exited, status = waitpid_timeout(client_pid, args.interrupt_timeout)
            tail = bytes(buf[tail_start:])
            left_alt_screen = ALT_SCREEN_LEAVE in tail
            cursor_shown = CURSOR_SHOW in tail
            results["sigint_restores_terminal"] = left_alt_screen and cursor_shown
            if not left_alt_screen:
                problems.append("alternate screen was not left after SIGINT")
            if not cursor_shown:
                problems.append("cursor was not restored after SIGINT")

            # 4. The client exited cleanly rather than being killed.
            exit_code = None
            if not exited:
                problems.append("client did not exit within %ss of SIGINT"
                                 % args.interrupt_timeout)
            elif status is None:
                problems.append("client was already reaped before its exit status could be read")
            elif os.WIFEXITED(status):
                exit_code = os.WEXITSTATUS(status)
            elif os.WIFSIGNALED(status):
                problems.append("client was terminated by signal %d, not a clean exit"
                                 % os.WTERMSIG(status))
            results["clean_exit_code"] = exit_code
            if exit_code != 0:
                problems.append("client exit code was %r, expected 0" % (exit_code,))

            # This IS assertion 5: both halves above came from the one
            # SIGINT sent above, not from a second signal or a fallback kill.
            results["single_sigint_shared_handler"] = (
                results["sigint_restores_terminal"] and exit_code == 0)
            if not results["single_sigint_shared_handler"]:
                problems.append(
                    "a single SIGINT did not both restore the terminal and exit gracefully "
                    "(the Task 5 shared-handler production-shape gap)")
    finally:
        if client_pid > 0:
            cleanup_client(client_pid, problems)
            if client_group_alive(client_pid):
                problems.append("client process group still alive")
        if master_fd >= 0:
            try:
                os.close(master_fd)
            except OSError:
                pass
        server_code, how = terminate_tree(server)
        if how not in ("already", "graceful"):
            problems.append("server cleanup was not graceful (%s)" % how)
        if process_group_alive(server):
            problems.append("server process group still alive")
        close_proc(server)
        if not wait_port_released(port, 15):
            problems.append("server port was not released")

    with open(transcript_path, "wb") as handle:
        handle.write(bytes(buf))

    ok = not problems
    print("[AUTOTEST] TUI_BOARD_PTY_RESULT status=%s port=%d transcript=%s"
          % ("PASS" if ok else "FAIL", port, transcript_path))
    for name in ("alt_screen_enter", "resize_repaint", "sigint_restores_terminal",
                 "clean_exit_code", "single_sigint_shared_handler"):
        print("  - %s: %s" % (name, results.get(name)))
    for problem in problems:
        print("  ! %s" % problem, file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
