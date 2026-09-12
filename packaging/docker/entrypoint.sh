#!/bin/sh

set -eu

# The image sets QSAN_ASSET_ROOT=/data, so the server reads its rules content
# from the volume and legacy relative writes land there too. Web clients are
# only admitted when that tree holds real files: the declared-v2 content scan
# rejects symlinks. Each start therefore replaces /data/<resource> with a fresh
# read-only copy of the image's content. A marker beside the copy (never inside
# it, where the scan would see it) records that this image owns the name.

remove_tree()
{
    if [ -L "$1" ]; then
        rm "$1"
    elif [ -e "$1" ]; then
        chmod -R u+w "$1"
        rm -rf "$1"
    fi
}

install_resource()
{
    resource_name=$1
    source_path="/opt/qsanguosha/$resource_name"
    target_path="/data/$resource_name"
    staging_path="/data/.$resource_name.staging"
    marker_path="/data/.qsanguosha-managed-$resource_name"

    if [ ! -d "$source_path" ]; then
        echo "Required runtime resource is missing: $source_path" >&2
        exit 1
    fi

    if [ -L "$target_path" ]; then
        # Earlier images linked these names to /opt; migrate such a link.
        current_target=$(readlink "$target_path")
        if [ "$current_target" != "$source_path" ]; then
            echo "Reserved path $target_path points to $current_target, expected $source_path" >&2
            exit 1
        fi
        rm "$target_path"
    elif [ -e "$target_path" ] && [ ! -e "$marker_path" ]; then
        echo "Reserved runtime resource path already exists and is not managed by this image: $target_path" >&2
        exit 1
    fi

    remove_tree "$staging_path"
    cp -R "$source_path" "$staging_path"
    chmod -R a-w "$staging_path"
    : > "$marker_path"
    remove_tree "$target_path"
    mv "$staging_path" "$target_path"
}

install_resource lua
install_resource extensions
install_resource lang

exec /opt/qsanguosha/bin/qsanguosha_server "$@"
