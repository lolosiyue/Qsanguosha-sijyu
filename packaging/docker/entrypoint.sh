#!/bin/sh

set -eu

ensure_resource_link()
{
    resource_name=$1
    target_path=$2
    link_path="/data/$resource_name"

    if [ ! -e "$target_path" ]; then
        echo "Required runtime resource is missing: $target_path" >&2
        exit 1
    fi

    if [ -L "$link_path" ]; then
        current_target=$(readlink "$link_path")
        if [ "$current_target" != "$target_path" ]; then
            echo "Reserved path $link_path points to $current_target, expected $target_path" >&2
            exit 1
        fi
        return
    fi

    if [ -e "$link_path" ]; then
        echo "Reserved runtime resource path already exists and is not a symlink: $link_path" >&2
        exit 1
    fi

    ln -s "$target_path" "$link_path"
}

ensure_resource_link lua /opt/qsanguosha/lua
ensure_resource_link extensions /opt/qsanguosha/extensions

exec /opt/qsanguosha/bin/qsanguosha_server "$@"
