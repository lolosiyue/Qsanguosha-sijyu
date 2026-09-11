# Docker Dedicated Server

The Docker image contains only the Linux dedicated server and its immutable
Lua/extension runtime. It uses an Ubuntu 24.04 builder and a separate minimal
Ubuntu 24.04 runtime stage.

## Docker Compose

The checked-in [`compose.yaml`](../compose.yaml) builds the image, publishes TCP
port 9527 and WebSocket port 9528, bind-mounts the Docker configuration read-only, and keeps writable
state in a named volume:

```bash
docker compose up -d
docker compose logs -f
docker compose stop
```

Compose uses `restart: unless-stopped` and allows 45 seconds for the server's
graceful shutdown path.

## Direct Docker usage

Build the runtime image and prepare an editable configuration:

```bash
docker build -t qsanguosha-server .
cp packaging/docker/server.ini server.ini
```

Run the server:

```bash
docker run -d \
  --name qsanguosha \
  -p 9527:9527 \
  -p 9528:9528 \
  -v qsanguosha-data:/data \
  -v "$PWD/server.ini:/config/server.ini:ro" \
  qsanguosha-server
```

Follow logs and stop it gracefully:

```bash
docker logs -f qsanguosha
docker stop --time 45 qsanguosha
```

The default configuration binds `any-ipv4`, so normal bridge networking works
with `-p 9527:9527 -p 9528:9528`; host networking and privileged mode are not required.

## Filesystem and process contract

- `/opt/qsanguosha/bin/qsanguosha_server` is the immutable server binary.
- `/opt/qsanguosha/lua`, `/opt/qsanguosha/extensions` and `/opt/qsanguosha/lang`
  are bundled immutable runtime resources. Extensions are fetched during the
  image build from the commit pinned by `QSAN_EXTENSIONS_REF`, which must match
  the `extension_names` list in `lua/config.lua`.
- `/config/server.ini` is the server configuration. The Compose bind mount is
  read-only.
- `/data` is the asset root (`QSAN_ASSET_ROOT`), the working directory and the
  persistent writable state/save volume. Relative legacy writes such as
  `GER.lua` therefore resolve to `/data/GER.lua`.
- `/data/lua`, `/data/extensions` and `/data/lang` are read-only copies of the
  resources under `/opt/qsanguosha`, replaced by the entrypoint on every start
  and tracked by `/data/.qsanguosha-managed-<name>` markers. They are copies
  rather than symlinks because Web signup requires a `declared-v1` rules
  identity, whose content scan rejects symlinks. Volumes created by earlier
  images, which held symlinks here, are migrated automatically. These names are
  reserved: an unmanaged file or directory at one of them stops the container.
- The server image leaves out `lua/chat_config.lua` (GUI chat phrases) and the
  unused `lua/lib/sqlite3.lua`, because `lua/config.lua` does not declare them.
- `HOME` and `XDG_CONFIG_HOME` also resolve under `/data`, keeping runtime state
  in the persistent volume. Because the asset root is explicit, per-user data
  such as `record/` and AI data lives under `/data/.local/share/QSanguosha`.

The image runs as the dedicated `qsanguosha` account with UID/GID `9527:9527`.
Bind-mounted host data must be writable by that identity. The entrypoint uses
`exec`, leaving `qsanguosha_server` as PID 1. Logs go to stdout/stderr and are
available through `docker logs`; no image-local log file is enabled by default.
`docker stop` sends `SIGTERM` directly to the server's graceful shutdown path.

## Packaging smoke test

Run the same final-image checks used by Docker CI:

```bash
bash tools/ci/docker-server-smoke.sh
```

The smoke test builds the final image, validates `--version` and
`--check-config`, verifies non-root execution and runtime-image hygiene, starts
a published container, performs the version/setup/signup protocol sequence
(requiring the hello to advertise a sealed `declared-v1` rules identity, without
which every Web client is refused),
uses `docker stop` to verify graceful SIGTERM handling, and recreates a
container against the same named volume to verify persistence. Diagnostics are
written to `ci-logs/docker/`.

This first image is x86_64 CI packaging only. It is not published to a registry,
does not claim multi-architecture support, and does not include a management or
health API.
