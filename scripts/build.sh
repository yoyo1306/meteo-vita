#!/usr/bin/env bash
# Build Météo Vita VPK inside WSL
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export VITASDK=/usr/local/vitasdk
export PATH="$VITASDK/bin:$PATH"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# vita-pack-vpk / CMake cassent les chemins avec espaces (ex. "Projects Cursor")
BUILD_ROOT="$ROOT"
if [[ "$ROOT" == *" "* ]]; then
  BUILD_ROOT="/tmp/meteo-vita"
  ln -sfn "$ROOT" "$BUILD_ROOT"
fi

RELEASE_DIR="$ROOT/A_INSTALLER_MOI"
RELEASE_NAME="Meteo_Vita__INSTALLER_MOI.vpk"

cd "$BUILD_ROOT"
python3 "$BUILD_ROOT/scripts/gen_livearea_bg.py"
mkdir -p build
cd build

cmake ..
make -j"$(nproc)"

# Dossier unique + nom impossible à confondre
mkdir -p "$RELEASE_DIR"
# Nettoie d'anciens VPK dans ce dossier (un seul fichier safe)
rm -f "$RELEASE_DIR"/*.vpk
cp -f "$BUILD_ROOT/build/meteo_vita.vpk" "$RELEASE_DIR/$RELEASE_NAME"

# Petit mémo à côté du VPK
cat > "$RELEASE_DIR/LISEZMOI.txt" << EOF
Météo Vita — FICHIER À INSTALLER
================================
Installe UNIQUEMENT :

  $RELEASE_NAME

Ignore les autres .vpk ailleurs (build/, ux0:data/, etc.).

Sur la Vita (VitaShell) :
1) Copie ce fichier dans ux0:data/
2) Sélectionne-le → Install
3) Bulle LiveArea : "Météo Vita" (Title ID METV00001)

START = quitter l'app.
EOF

echo ""
echo "=== Build OK ==="
echo "INSTALLE UNIQUEMENT CE FICHIER :"
echo "  $RELEASE_DIR/$RELEASE_NAME"
ls -lh "$RELEASE_DIR/$RELEASE_NAME"
