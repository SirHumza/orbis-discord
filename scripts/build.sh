#!/bin/bash
# build.sh - orbis-discord: compile PS4 ELF (+ fself).
# Usage: ./scripts/build.sh [elf|fself|all]   default: all
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SDK="${OO_PS4_TOOLCHAIN:-/Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain}"
if [ -z "${LLVM_HOME:-}" ]; then
  for cand in /opt/homebrew/opt/llvm /usr/local/opt/llvm /usr/local/llvm; do
    if [ -x "$cand/bin/clang" ]; then LLVM_HOME="$cand"; break; fi
  done
fi
LLVM_BIN="${LLVM_HOME:-/usr/local/opt/llvm}/bin"
if [ -z "${LLD:-}" ]; then
  for cand in /opt/homebrew/opt/llvm/bin/ld.lld /usr/local/opt/llvm/bin/ld.lld "$LLVM_BIN/ld.lld"; do
    if [ -x "$cand" ] && "$cand" --version >/dev/null 2>&1; then LLD="$cand"; break; fi
  done
  [ -n "${LLD:-}" ] || LLD="$LLVM_BIN/ld.lld"
fi
export PATH="$LLVM_BIN:$SDK/bin/macos:$PATH"
CC="$LLVM_BIN/clang"
MODE="${1:-all}"

fail() { echo "BUILD FAIL: $1" >&2; exit 1; }
[ -x "$CC" ] || fail "clang not found at $CC"
[ -x "$LLD" ] || fail "ld.lld not found at $LLD"
[ -d "$SDK" ] || fail "OpenOrbis SDK not found at $SDK"
[ -f "$SDK/lib/crt1.o" ] || fail "missing $SDK/lib/crt1.o"
[ -f "$SDK/link.x" ] || fail "missing $SDK/link.x"

TARGET="x86_64-pc-freebsd12-elf"
CFLAGS="--target=$TARGET -fPIC -std=gnu11 -O2 -Wall -Wno-unused \
        -Wno-int-conversion -Wno-incompatible-pointer-types \
        -DMBEDTLS_NO_PLATFORM_ENTROPY -D__PS4__ \
        -Isrc/core -Isrc/model \
        -isystem $SDK/include -Ithird_party/mbedtls/include"
LIBS="-lc -lkernel -lSceNet -lSceNetCtl -lSceSysmodule -lSceUserService -lSDL2"
LDFLAGS="-m elf_x86_64 -pie --eh-frame-hdr -L$SDK/lib $LIBS $SDK/lib/crt1.o --script $SDK/link.x"
export OO_PS4_TOOLCHAIN="$SDK"

OUT="$ROOT/build"; mkdir -p "$OUT"
APP_SRCS="main log compat b64 jsonlite gateway events http rest remoteauth tls ws"
MODEL_SRCS="store snowflake"

echo "=== compiling app ==="
for f in $APP_SRCS; do
  "$CC" $CFLAGS -c -o "$OUT/app_$f.o" "src/core/$f.c" 2>/dev/null || "$CC" $CFLAGS -c -o "$OUT/app_$f.o" "src/$f.c" || fail "compile $f"
done
for f in $MODEL_SRCS; do
  "$CC" $CFLAGS -c -o "$OUT/model_$f.o" "src/model/$f.c" || fail "compile model/$f"
done

echo "=== mbedtls ==="
for f in $(find third_party/mbedtls/library -name '*.c' ! -name 'net_sockets.c' ! -name 'timing.c' ! -name 'entropy_poll.c' | sort); do
  o="$OUT/mbed_$(basename "${f%.c}").o"
  "$CC" $CFLAGS -c -o "$o" "$f" || fail "mbedtls $f"
done

echo "=== linking ==="
"$LLD" "$OUT"/*.o -o "$OUT/orbis-discord.elf" $LDFLAGS || fail "link"
echo "ELF -> $OUT/orbis-discord.elf ($(stat -f%z "$OUT/orbis-discord.elf" 2>/dev/null || stat -c%s "$OUT/orbis-discord.elf") bytes)"

if [ "$MODE" = "fself" ] || [ "$MODE" = "all" ]; then
  "$SDK/bin/macos/create-fself-macos" -in="$OUT/orbis-discord.elf" \
      -out="$OUT/orbis-discord.fself" --eboot "$OUT/orbis-discord-eboot.bin" --paid 0x3800000000000011 || fail "fself"
  echo "FSELF -> $OUT/orbis-discord.fself"
fi
