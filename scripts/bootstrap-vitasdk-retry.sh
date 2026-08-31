#!/usr/bin/env bash
set -euo pipefail
export PATH="/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
export VITASDK=/usr/local/vitasdk

echo "==> Dépendances (bzip2, etc.)"
sudo apt-get update -qq
sudo apt-get install -y bzip2 xz-utils unzip make git cmake python3 curl wget ca-certificates

echo "==> Nettoyage"
sudo rm -rf /usr/local/vitasdk

echo "==> Clone vdpm"
cd /tmp
rm -rf vdpm
git clone --depth 1 https://github.com/vitasdk/vdpm
cd vdpm

echo "==> Bootstrap VitaSDK..."
./bootstrap-vitasdk.sh

export PATH="$VITASDK/bin:$PATH"
hash -r

echo "==> Paquets vdpm"
vdpm zlib || true
vdpm libpng || true
vdpm libvita2d || true
vdpm curl || true
vdpm openssl || true

echo ""
echo "=== TERMINE ==="
ls "$VITASDK" | head
command -v arm-vita-eabi-gcc
arm-vita-eabi-gcc --version | head -n1
