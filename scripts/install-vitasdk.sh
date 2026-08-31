#!/usr/bin/env bash
# Install VitaSDK + deps for Météo Vita (WSL Ubuntu)
set -euo pipefail

echo "==> [1/4] Mise à jour apt + dépendances"
sudo apt-get update
sudo apt-get install -y make git cmake python3 curl wget ca-certificates unzip

echo "==> [2/4] Variables d'environnement VITASDK"
if ! grep -q 'VITASDK=/usr/local/vitasdk' "$HOME/.bashrc" 2>/dev/null; then
  {
    echo ''
    echo '# VitaSDK'
    echo 'export VITASDK=/usr/local/vitasdk'
    echo 'export PATH=$VITASDK/bin:$PATH'
  } >> "$HOME/.bashrc"
fi
export VITASDK=/usr/local/vitasdk
export PATH=$VITASDK/bin:$PATH

echo "==> [3/4] Bootstrap VitaSDK (vdpm)"
TMPDIR="$(mktemp -d)"
cd "$TMPDIR"
if [ ! -d /usr/local/vitasdk ]; then
  git clone --depth 1 https://github.com/vitasdk/vdpm
  cd vdpm
  ./bootstrap-vitasdk.sh
else
  echo "VitaSDK déjà présent dans /usr/local/vitasdk — skip bootstrap"
fi

echo "==> [4/4] Paquets utiles (vita2d, curl, etc.)"
# vdpm may be in PATH after bootstrap
hash -r
if command -v vdpm >/dev/null 2>&1; then
  vdpm zlib || true
  vdpm libpng || true
  vdpm libvita2d || true
  vdpm curl || true
  vdpm openssl || true
  vdpm taihen || true
else
  echo "ATTENTION: vdpm introuvable dans PATH — recharge le shell puis relance les vdpm"
fi

echo ""
echo "=== Installation terminée ==="
echo "VITASDK=$VITASDK"
command -v arm-vita-eabi-gcc && arm-vita-eabi-gcc --version | head -n1
echo "Ferme cette fenêtre et dis à l'assistant que c'est OK."
read -r -p "Appuie sur Entrée pour fermer..."
