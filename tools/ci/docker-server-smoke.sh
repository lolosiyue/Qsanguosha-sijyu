#!/usr/bin/env bash

set -Eeuo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
artifact_dir=${QSAN_DOCKER_ARTIFACT_DIR:-$root/ci-logs/docker}
image=${QSAN_DOCKER_IMAGE:-qsanguosha-server:smoke}
suffix=${GITHUB_RUN_ID:-local}-${GITHUB_RUN_ATTEMPT:-0}-$$
server_container=qsanguosha-server-smoke-$suffix
persistence_container=qsanguosha-persistence-smoke-$suffix
volume=qsanguosha-data-smoke-$suffix
docker_ready=false

mkdir -p "$artifact_dir"
exec > >(tee "$artifact_dir/docker-server-smoke.log") 2>&1

log()
{
    printf '[docker-server-smoke] %s\n' "$*"
}

container_exists()
{
    [[ -n $1 ]] || return 1
    docker container inspect "$1" >/dev/null 2>&1
}

capture_server_diagnostics()
{
    if container_exists "$server_container"; then
        docker logs "$server_container" >"$artifact_dir/server.log" 2>&1 || true
        docker container inspect "$server_container" \
            >"$artifact_dir/server-inspect.json" 2>&1 || true
    fi
}

cleanup()
{
    status=$?
    trap - EXIT
    set +e
    if $docker_ready; then
        capture_server_diagnostics
        if container_exists "$server_container"; then
            docker rm --force "$server_container" >/dev/null
        fi
        if container_exists "$persistence_container"; then
            docker rm --force "$persistence_container" >/dev/null
        fi
        if [[ -n $volume ]]; then
            docker volume rm "$volume" >/dev/null 2>&1
        fi
    fi
    exit "$status"
}
trap cleanup EXIT

command -v docker >/dev/null || {
    echo 'docker is required' >&2
    exit 1
}
command -v python3 >/dev/null || {
    echo 'python3 is required for the protocol smoke' >&2
    exit 1
}
docker info >/dev/null
docker_ready=true

log "building final runtime image $image"
docker build --file "$root/Dockerfile" --tag "$image" "$root"
docker image inspect "$image" >"$artifact_dir/image-inspect.json"
image_size_bytes=$(docker image inspect --format '{{.Size}}' "$image")
image_size_mib=$(python3 -c \
    'import sys; print(f"{int(sys.argv[1]) / 1048576:.1f}")' "$image_size_bytes")
log "final image size: $image_size_bytes bytes ($image_size_mib MiB)"

log 'checking version and Docker configuration'
version_output=$(docker run --rm "$image" --version)
printf '%s\n' "$version_output"
[[ $version_output =~ ^qsanguosha_server[[:space:]][0-9]+ ]] || {
    echo 'unexpected --version output' >&2
    exit 1
}
config_output=$(docker run --rm "$image" \
    --config /config/server.ini --check-config 2>&1)
printf '%s\n' "$config_output"
[[ $config_output == *'Configuration OK'* ]] || {
    echo '--check-config did not report success' >&2
    exit 1
}

log 'checking non-root metadata, immutable assets, and image hygiene'
runtime_user=$(docker image inspect --format '{{.Config.User}}' "$image")
[[ $runtime_user == '9527:9527' ]] || {
    echo "unexpected image user: $runtime_user" >&2
    exit 1
}
[[ $(docker image inspect --format '{{.Config.WorkingDir}}' "$image") == /data ]]
[[ $(docker image inspect --format '{{.Config.StopSignal}}' "$image") == SIGTERM ]]
[[ $(docker image inspect --format '{{json .Config.ExposedPorts}}' "$image") \
    == '{"9527/tcp":{}}' ]]
[[ $(docker image inspect --format '{{json .Config.Volumes}}' "$image") \
    == '{"/data":{}}' ]]
docker run --rm --entrypoint /bin/sh "$image" -ec '
    test "$(id -u)" -eq 9527
    test "$(id -g)" -eq 9527
    test -f /opt/qsanguosha/lua/sanguosha.lua
    test -f /opt/qsanguosha/lua/ai/smart-ai.lua
    test -f /opt/qsanguosha/lua/ai/isolated/ask-for-use-card.lua
    test -d /opt/qsanguosha/extensions
    extension_count=0
    for extension in /opt/qsanguosha/extensions/*.lua; do
        test -f "$extension" || continue
        extension_count=$((extension_count + 1))
    done
    test "$extension_count" -gt 0
    test ! -w /opt/qsanguosha/lua
    test ! -w /opt/qsanguosha/extensions
    test ! -w /config/server.ini
    for tool in gcc g++ cmake ninja swig git; do
        if command -v "$tool" >/dev/null 2>&1; then
            echo "build tool leaked into runtime image: $tool" >&2
            exit 1
        fi
    done
    for header_dir in /usr/include/qt6 /usr/include/*/qt6; do
        if test -e "$header_dir"; then
            echo "Qt development headers leaked into runtime image: $header_dir" >&2
            exit 1
        fi
    done
    test ! -e /src
    test ! -e /build
    test ! -e /opt/qsanguosha/include
    test ! -e /opt/qsanguosha/bin/qsanguosha_network_integration_tests
'

docker run --rm --entrypoint /usr/bin/ldd "$image" \
    /opt/qsanguosha/bin/qsanguosha_server \
    | tee "$artifact_dir/runtime-ldd.txt"
if grep -q 'not found' "$artifact_dir/runtime-ldd.txt"; then
    echo 'runtime library resolution failed' >&2
    exit 1
fi

log "creating named volume $volume and starting a real container"
docker volume create "$volume" >/dev/null
docker run --detach \
    --name "$server_container" \
    --mount "type=volume,source=$volume,target=/data" \
    --publish 127.0.0.1::9527 \
    "$image" >/dev/null
[[ $(docker container inspect --format '{{.HostConfig.Privileged}}' \
    "$server_container") == false ]]
[[ $(docker container inspect --format '{{.HostConfig.NetworkMode}}' \
    "$server_container") != host ]]

listening=false
for _ in $(seq 1 60); do
    running=$(docker container inspect --format '{{.State.Running}}' "$server_container")
    if [[ $running != true ]]; then
        echo 'server container exited before reaching the listening state' >&2
        docker logs "$server_container" >&2 || true
        exit 1
    fi
    server_output=$(docker logs "$server_container" 2>&1)
    if [[ $server_output == *'Listening on '*':9527'* ]]; then
        listening=true
        break
    fi
    sleep 1
done
$listening || {
    echo 'server did not listen within 60 seconds' >&2
    exit 1
}

host_port=$(docker container inspect \
    --format '{{(index (index .NetworkSettings.Ports "9527/tcp") 0).HostPort}}' \
    "$server_container")
[[ $host_port =~ ^[0-9]+$ ]] || {
    echo "invalid published port: $host_port" >&2
    exit 1
}
log "published endpoint is 127.0.0.1:$host_port"

python3 "$root/tools/ci/docker-protocol-smoke.py" \
    --host 127.0.0.1 \
    --port "$host_port" \
    --name docker-smoke-player \
    --protocol-header "$root/src/core/protocol.h"

joined=false
for _ in $(seq 1 10); do
    server_output=$(docker logs "$server_container" 2>&1)
    if [[ $server_output == *' joined name=docker-smoke-player'* ]]; then
        joined=true
        break
    fi
    sleep 1
done
$joined || {
    echo 'server logs did not record the signed-up waiting-room player' >&2
    exit 1
}
if [[ $server_output == *'AI Lua runtime disabled:'* ]]; then
    echo 'RoomRuntime disabled the isolated AI runtime' >&2
    exit 1
fi

log 'checking PID 1 identity and writable persistent data'
docker exec "$server_container" /bin/sh -ec '
    set -- $(grep "^Uid:" /proc/1/status)
    test "$2" -eq 9527
    test "$(readlink /proc/1/exe)" = /opt/qsanguosha/bin/qsanguosha_server
    test "$(readlink /data/lua)" = /opt/qsanguosha/lua
    test "$(readlink /data/extensions)" = /opt/qsanguosha/extensions
    test -w /data
    printf "%s\n" docker-persistence-ok > /data/.docker-smoke-sentinel
'

log 'stopping with Docker SIGTERM and a 45-second timeout'
docker stop --time 45 "$server_container" >/dev/null
exit_code=$(docker container inspect --format '{{.State.ExitCode}}' "$server_container")
oom_killed=$(docker container inspect --format '{{.State.OOMKilled}}' "$server_container")
[[ $exit_code == 0 && $oom_killed == false ]] || {
    echo "container did not exit normally: exit=$exit_code oom_killed=$oom_killed" >&2
    exit 1
}
server_output=$(docker logs "$server_container" 2>&1)
[[ $server_output == *'Shutdown requested by signal 15'* ]] || {
    echo 'server did not acknowledge Docker SIGTERM' >&2
    exit 1
}
[[ $server_output == *' INFO server stopping'* ]] || {
    echo 'server did not enter graceful stopping state' >&2
    exit 1
}
[[ $server_output == *' INFO server stopped exit_code=0'* ]] || {
    echo 'server did not report a normal stopped state' >&2
    exit 1
}
capture_server_diagnostics
docker rm "$server_container" >/dev/null
server_container=

log 'recreating a container with the same named volume'
docker create \
    --name "$persistence_container" \
    --mount "type=volume,source=$volume,target=/data" \
    --entrypoint /bin/sh \
    "$image" -ec '
        test "$(cat /data/.docker-smoke-sentinel)" = docker-persistence-ok
        test "$(readlink /data/lua)" = /opt/qsanguosha/lua
        test "$(readlink /data/extensions)" = /opt/qsanguosha/extensions
    ' >/dev/null
docker start --attach "$persistence_container"
docker rm "$persistence_container" >/dev/null
persistence_container=
docker volume rm "$volume" >/dev/null
volume=

log 'PASS: build, config, non-root, assets, protocol/signup, SIGTERM, persistence, and hygiene'
