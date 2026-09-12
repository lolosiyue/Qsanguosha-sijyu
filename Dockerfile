FROM ubuntu:24.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG CMAKE_BUILD_PARALLEL_LEVEL=2
ARG QSAN_EXTENSIONS_REPO=https://github.com/lolosiyue/extensions.git
# Pinned to the extension set that lua/config.lua declares in extension_names.
# Web signup is refused when an extension file is undeclared or missing, so
# bump this commit together with that list.
ARG QSAN_EXTENSIONS_REF=f2071da7350a2bb97131197ed73c74aa3ec6a2f4

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ca-certificates \
        cmake \
        g++ \
        gcc \
        git \
        ninja-build \
        python3-venv \
        qt6-base-dev \
        qt6-websockets-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN python3 -m venv /opt/qsanguosha-swig \
    && /opt/qsanguosha-swig/bin/python -m pip install \
        --disable-pip-version-check \
        --require-hashes \
        --requirement tools/ci/requirements.txt \
    && /opt/qsanguosha-swig/bin/swig -version

ENV PATH="/opt/qsanguosha-swig/bin:${PATH}"

RUN QSAN_EXTENSIONS_REPO="${QSAN_EXTENSIONS_REPO}" \
    QSAN_EXTENSIONS_REF="${QSAN_EXTENSIONS_REF}" \
    tools/ci/fetch-extensions.sh /src

RUN cmake -S /src -B /build -G Ninja \
        -DBUILD_TESTING=OFF \
        -DQSAN_BUILD_TUI=OFF \
        -DCMAKE_BUILD_TYPE=RelWithDebInfo \
        -DCMAKE_C_COMPILER=gcc \
        -DCMAKE_CXX_COMPILER=g++ \
        -DCMAKE_INSTALL_PREFIX=/opt/qsanguosha \
    && cmake --build /build \
        --target qsanguosha_server \
        --parallel "${CMAKE_BUILD_PARALLEL_LEVEL}" \
    && DESTDIR=/staging cmake --install /build \
        --prefix /opt/qsanguosha \
        --strip \
    && test -x /staging/opt/qsanguosha/bin/qsanguosha_server \
    && test -f /staging/opt/qsanguosha/share/qsanguosha/lua/sanguosha.lua \
    && test -f /staging/opt/qsanguosha/share/qsanguosha/lua/ai/smart-ai.lua \
    && test -f /staging/opt/qsanguosha/share/qsanguosha/lua/ai/isolated/ask-for-use-card.lua \
    && test -d /staging/opt/qsanguosha/share/qsanguosha/extensions \
    && test -f /staging/opt/qsanguosha/share/qsanguosha/lang/zh_CN/Common.lua \
    && ldd /staging/opt/qsanguosha/bin/qsanguosha_server \
    && ! ldd /staging/opt/qsanguosha/bin/qsanguosha_server | grep -q 'not found'

# The Web admission gate (declared-v2) rejects Lua that lua/config.lua does not
# declare. chat_config.lua only feeds the GUI chat panel and lib/sqlite3.lua has
# no caller, so the dedicated server image leaves both out.
RUN rm /staging/opt/qsanguosha/share/qsanguosha/lua/chat_config.lua \
        /staging/opt/qsanguosha/share/qsanguosha/lua/lib/sqlite3.lua


FROM ubuntu:24.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        libqt6network6 \
        libqt6websockets6 \
    && rm -rf /var/lib/apt/lists/* \
    && groupadd --gid 9527 qsanguosha \
    && useradd --uid 9527 --gid 9527 \
        --home-dir /data --no-create-home \
        --shell /usr/sbin/nologin qsanguosha \
    && install -d --owner=9527 --group=9527 --mode=0750 /data

COPY --from=builder \
    /staging/opt/qsanguosha/bin/qsanguosha_server \
    /opt/qsanguosha/bin/qsanguosha_server
COPY --from=builder \
    /staging/opt/qsanguosha/share/qsanguosha/lua \
    /opt/qsanguosha/lua
COPY --from=builder \
    /staging/opt/qsanguosha/share/qsanguosha/extensions \
    /opt/qsanguosha/extensions
COPY --from=builder \
    /staging/opt/qsanguosha/share/qsanguosha/lang \
    /opt/qsanguosha/lang
COPY packaging/docker/server.ini /config/server.ini
COPY packaging/docker/entrypoint.sh /usr/local/bin/qsanguosha-entrypoint

RUN chmod -R a-w /opt/qsanguosha \
    && chmod 0555 /opt/qsanguosha/bin/qsanguosha_server \
    && chmod 0444 /config/server.ini \
    && chmod 0555 /usr/local/bin/qsanguosha-entrypoint \
    && ldd /opt/qsanguosha/bin/qsanguosha_server \
    && ! ldd /opt/qsanguosha/bin/qsanguosha_server | grep -q 'not found'

ENV HOME=/data \
    XDG_CONFIG_HOME=/data/.config \
    QSAN_ASSET_ROOT=/data \
    PATH="/opt/qsanguosha/bin:${PATH}"

WORKDIR /data
VOLUME ["/data"]

EXPOSE 9527/tcp
EXPOSE 9528/tcp
STOPSIGNAL SIGTERM

USER 9527:9527

ENTRYPOINT ["/usr/local/bin/qsanguosha-entrypoint"]
CMD ["--config", "/config/server.ini", "--log-level", "info", "--log-format", "text"]
