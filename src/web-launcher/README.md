# QSanguosha portable Web launcher

`web-launcher.cpp` is a small Windows GUI-subsystem launcher for a complete
offline Web bundle. Put the resulting executable beside `index.html`; the
launcher serves that directory tree and opens the default browser at:

```text
http://127.0.0.1:9529/
```

Port `9529` is fixed so browser `localStorage` preferences remain stable. The
existing WebSocket game server conventionally uses port `9528`; this launcher
does not start or proxy a game server.

The source uses only Win32, Winsock2, and the standard C++17 library. A Visual
Studio GUI target should link `ws2_32.lib`, `shell32.lib`, `user32.lib`, and
`ole32.lib` and use the static
CRT if a self-contained executable is desired. The bundle should contain the
production `index.html`, JavaScript/WASM artifacts, Lua/AI files, and its
co-located `assets/` and `audio/` trees.

Requests are limited to GET/HEAD, URL decoding is validated as UTF-8, traversal
and reparse-point paths are rejected, and files are streamed in 64 KiB chunks.
At most sixteen clients are handled concurrently; when that capacity is full,
the listener leaves new connections in the Winsock backlog. Idle sockets have bounded
receive/send timeouts and are explicitly woken during shutdown. The Host header
must be exactly `127.0.0.1:9529`. Responses include the COOP, COEP, and CORP
headers required by the browser WASM runtime.

Browser launch initializes COM on the calling worker and uses
`ShellExecuteExW` with `SEE_MASK_NOASYNC` because the socket worker has no
Windows message loop. Launch failures are displayed with the local URL while
static serving stays available.
