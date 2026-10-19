#!/bin/bash
# pkg.sh - build orbis-discord.fself/eboot, stage a PS4 package, run PkgTool.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

SDK="${OO_PS4_TOOLCHAIN:-/Users/mac/PS4Toolchain/OpenOrbis/PS4Toolchain}"
case "$(uname -s)" in
  Darwin) CDIR=macos ;;
  Linux)  CDIR=linux ;;
  *)      CDIR=windows ;;
esac
TOOL="$SDK/bin/$CDIR"
CONTENT_ID="IV0000-ORDS00001_00-ORBISDISCORD0001"
TITLE_ID="ORDS00001"
TITLE="orbis-discord"
VERSION="01.00"

fail(){ echo "PKG FAIL: $1" >&2; exit 1; }
[ -x "$TOOL/PkgTool.Core" ] || fail "PkgTool.Core not found at $TOOL"
[ -x "$TOOL/create-gp4" ] || fail "create-gp4 not found at $TOOL"

./scripts/build.sh fself

OUT="$ROOT/build"
PKG="$OUT/pkg"
rm -rf "$PKG"
mkdir -p "$PKG/sce_sys/about" "$PKG/sce_module"
cp "$OUT/orbis-discord-eboot.bin" "$PKG/eboot.bin"
cp packaging/sce_sys/icon0.png packaging/sce_sys/pic1.png "$PKG/sce_sys/"
cp packaging/sce_sys/about/right.sprx "$PKG/sce_sys/about/"
cp packaging/sce_module/libc.prx packaging/sce_module/libSceFios2.prx "$PKG/sce_module/"

SFO="$PKG/sce_sys/param.sfo"
PT="$TOOL/PkgTool.Core"
"$PT" sfo_new "$SFO" >/dev/null || fail sfo_new
"$PT" sfo_setentry "$SFO" APP_TYPE --type Integer --maxsize 4 --value 1 >/dev/null
"$PT" sfo_setentry "$SFO" APP_VER --type Utf8 --maxsize 8 --value "$VERSION" >/dev/null
"$PT" sfo_setentry "$SFO" ATTRIBUTE --type Integer --maxsize 4 --value 0 >/dev/null
"$PT" sfo_setentry "$SFO" CATEGORY --type Utf8 --maxsize 4 --value 'gd' >/dev/null
"$PT" sfo_setentry "$SFO" CONTENT_ID --type Utf8 --maxsize 48 --value "$CONTENT_ID" >/dev/null
"$PT" sfo_setentry "$SFO" DOWNLOAD_DATA_SIZE --type Integer --maxsize 4 --value 0 >/dev/null
"$PT" sfo_setentry "$SFO" SYSTEM_VER --type Integer --maxsize 4 --value 0 >/dev/null
"$PT" sfo_setentry "$SFO" TITLE --type Utf8 --maxsize 128 --value "$TITLE" >/dev/null
"$PT" sfo_setentry "$SFO" TITLE_ID --type Utf8 --maxsize 12 --value "$TITLE_ID" >/dev/null
"$PT" sfo_setentry "$SFO" VERSION --type Utf8 --maxsize 8 --value "$VERSION" >/dev/null

cd "$PKG"
"$TOOL/create-gp4" -out pkg.gp4 --content-id="$CONTENT_ID" \
  --files "eboot.bin sce_sys/icon0.png sce_sys/pic1.png sce_sys/param.sfo sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx" \
  >/dev/null || fail create-gp4
"$PT" pkg_build pkg.gp4 . >/dev/null || fail pkg_build
[ -f "$CONTENT_ID.pkg" ] || fail "pkg_build produced no $CONTENT_ID.pkg"
mv "$CONTENT_ID.pkg" "$OUT/orbis-discord.pkg"
echo "PKG -> $OUT/orbis-discord.pkg ($(stat -f%z "$OUT/orbis-discord.pkg" 2>/dev/null || stat -c%s "$OUT/orbis-discord.pkg") bytes)"
