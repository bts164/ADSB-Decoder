#!/usr/bin/env bash
# Generates the UI's Dart for the UI API into ui/frontend/lib/api/: the message classes of
# proto/adsb/api/v1/api.proto and of the framing, proto/wsrpc/v1/wsrpc.proto, and the services' clients,
# api.wsrpc.dart. Rerun after changing a .proto, and commit the result: the UI builds without protoc.
#
# protoc is Conan's, so the Dart and the server's C++ come from the same version: run `conan install` into
# build/<type> first (Release, unless BUILD_TYPE says otherwise). protoc-gen-dart is from
# `dart pub global activate protoc_plugin`, and needs dart on PATH. The clients are from tools/protoc-gen-wsrpc,
# run with the Python that `conan install` sets up for the C++ build, which has the protobuf package.
set -eo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
build=$root/build/${BUILD_TYPE:-Release}
env_script=$build/generators/conanbuild.sh
if [[ ! -f $env_script ]]; then
    echo "no $env_script: run conan install first" >&2
    exit 1
fi
# shellcheck disable=SC1090
source "$env_script"
set -u

plugin=${PROTOC_GEN_DART:-$HOME/.pub-cache/bin/protoc-gen-dart}
if ! command -v dart >/dev/null; then
    echo "dart isn't on PATH, and protoc-gen-dart needs it" >&2
    exit 1
fi

python=$build/conan_pyenv/bin/python
if ! "$python" -c "import google.protobuf" 2>/dev/null; then
    echo "no Python with the protobuf package at $python: run conan install first" >&2
    exit 1
fi
# protoc runs a plugin by path, so it runs this wrapper, which runs the plugin with that Python.
wrapper_dir=$(mktemp -d)
trap 'rm -r "$wrapper_dir"' EXIT
printf '#!/bin/sh\nexec "%s" "%s" "$@"\n' "$python" "$root/tools/protoc-gen-wsrpc" >"$wrapper_dir/protoc-gen-wsrpc"
chmod +x "$wrapper_dir/protoc-gen-wsrpc"

out=$root/ui/frontend/lib/api
mkdir -p "$out"
protoc --version
protoc -I"$root/proto" --plugin=protoc-gen-dart="$plugin" --dart_out="$out" \
    adsb/api/v1/api.proto wsrpc/v1/wsrpc.proto
protoc -I"$root/proto" --plugin=protoc-gen-wsrpc="$wrapper_dir/protoc-gen-wsrpc" \
    --wsrpc_opt=lang=dart,runtime=package:adsb_ui/wsrpc/channel.dart --wsrpc_out="$out" adsb/api/v1/api.proto
echo "generated into $out"
