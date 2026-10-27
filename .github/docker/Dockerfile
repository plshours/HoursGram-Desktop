# syntax=docker/dockerfile:1

# Telegram's own build environment dropped protobuf when upstream stopped
# needing it, but this fork is pinned to a tdesktop that still builds cld3,
# and cld3 asks for protobuf on Linux without any way to turn it off. So the
# shipped environment gets protobuf added back, built exactly the way the
# fork's own Dockerfile builds it, and nothing else changes.
ARG BASE=ghcr.io/telegramdesktop/tdesktop/centos_env:latest
FROM ${BASE}

USER root

RUN <<EOF
set -e
git clone -b v30.2 --depth=1 --recursive --shallow-submodules \
	https://github.com/protocolbuffers/protobuf.git /tmp/protobuf
cd /tmp/protobuf
cmake -B build . \
	-Dprotobuf_BUILD_TESTS=OFF \
	-Dprotobuf_BUILD_PROTOBUF_BINARIES=ON \
	-Dprotobuf_BUILD_LIBPROTOC=ON \
	-Dprotobuf_WITH_ZLIB=OFF
cmake --build build
cmake --install build
cd /
rm -rf /tmp/protobuf
EOF

USER user
WORKDIR /usr/src/tdesktop
