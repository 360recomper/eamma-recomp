#!/usr/bin/env bash
# Run codegen; while it fails on "target not in any function", seed each target in the
export LC_ALL=C.UTF-8
# functions file of the module that failed, then retry. Usage: tools/codegen_seed_loop.sh [rounds]
SDK=/e/ClaudeGame/EAMMA-rexglue-sdk/out/install/win-amd64
cd "$(dirname "$0")/.."
for i in $(seq 1 ${1:-30}); do
  "$SDK/bin/rexglue.exe" codegen eamma_manifest.toml > codegen.log 2>&1 && { echo "round $i: codegen ok"; exit 0; }
  mod=$(grep -oP "Analysis failed for '\K[^']+" codegen.log)
  case "$mod" in default) f=config/functions_default.toml;; *) f=config/functions_dll.toml;; esac
  n=0
  for a in $(grep -oP '^\s+0x\K[0-9A-F]{8}(?= from 0x[0-9A-F]+: .*target not in any function)' codegen.log | sort -u); do
    grep -q "^0x$a " $f && continue
    echo "0x$a = { name = \"sub_$a\" }  # codegen: branch target not in any function" >> $f; n=$((n+1))
  done
  echo "round $i: module '$mod' seeded $n"
  [ $n -eq 0 ] && { tail -20 codegen.log; exit 1; }
done
