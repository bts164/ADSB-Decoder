#!/usr/bin/env bash
# Generates the UI's Dart for the UI API (proto/adsb/api/v1/api.proto) into ui/frontend/lib/api/. Rerun after
# changing the .proto, and commit the result: the UI builds without protoc.
#
# protoc is Conan's, so the Dart and the server's C++ come from the same version: run `conan install` into
# build/<type> first (Release, unless BUILD_TYPE says otherwise). protoc-gen-dart is from
# `dart pub global activate protoc_plugin`, and needs dart on PATH.
set -eo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
env_script=$root/build/${BUILD_TYPE:-Release}/generators/conanbuild.sh
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

out=$root/ui/frontend/lib/api
mkdir -p "$out"
protoc --version
protoc -I"$root/proto" --plugin=protoc-gen-dart="$plugin" --dart_out="$out" adsb/api/v1/api.proto
echo "generated into $out"
