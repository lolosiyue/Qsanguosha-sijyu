# QSanguosha portable Web launcher

[`web-launcher.cpp`](web-launcher.cpp) is a small Windows GUI-subsystem launcher for a complete
offline Web bundle. Put the resulting executable beside `index.html`; the
launcher serves that directory tree and opens the default browser at:

```text
http://127.0.0.1:9529/
```

`kPort` in `web-launcher.cpp` fixes port `9529` so browser `localStorage`
preferences remain stable. The existing WebSocket game server conventionally
uses port `9528`; this launcher does not start or proxy a game server.

The source uses only Win32, Winsock2, and the standard C++17 library. A Visual
Studio GUI target should link `ws2_32.lib`, `shell32.lib`, `user32.lib`, and
`ole32.lib` and use the static
CRT if a self-contained executable is desired. The bundle should contain the
production `index.html`, JavaScript/WASM artifacts, Lua/AI files, and its
co-located `assets/` and `audio/` trees.

`handleClient()` accepts only GET/HEAD requests, `urlDecodePath()` validates
UTF-8 paths, and `safePath()` rejects traversal and reparse-point paths.
Responses stream files in 64 KiB chunks. `kMaxClients` limits concurrency to
sixteen clients; when that capacity is full,
the listener leaves new connections in the Winsock backlog. Idle sockets have bounded
receive/send timeouts and are explicitly woken during shutdown. The Host header
must be exactly `127.0.0.1:9529`. Responses include the COOP, COEP, and CORP
headers required by the browser WASM runtime.

Browser launch initializes COM on the calling worker and uses
`ShellExecuteExW` with `SEE_MASK_NOASYNC` because the socket worker has no
Windows message loop. Launch failures are displayed with the local URL while
static serving stays available.
