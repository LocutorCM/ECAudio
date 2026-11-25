#!/usr/bin/env bash
# Uso: scripts/make_dmg.sh "ruta/EC Audio.app" salida.dmg
set -euo pipefail
APP="$1"; OUT="$2"
STAGE="$(mktemp -d)"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
rm -f "$OUT"
hdiutil create -volname "EC Audio" -srcfolder "$STAGE" -ov -format UDZO "$OUT"
rm -rf "$STAGE"
echo "DMG creado: $OUT"
