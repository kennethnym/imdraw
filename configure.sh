#!/bin/bash

set -eu

script_path="$(realpath $0)"
root="$(dirname $script_path)"
pushd "$(dirname "$0")" > /dev/null

cat > .clangd <<- EOF
CompileFlags:
  Add:
    - -I$root/vendor/cimgui
    - -I$root/vendor/imgui
    - -I$root/vendor/sokol
    - -I$root/vendor/sokol/util
EOF

popd > /dev/null
