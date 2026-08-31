#!/usr/bin/env bash
set -euo pipefail
export PATH="/usr/bin:/bin"
ROOT="/mnt/c/Users/kevin/Projects/meteo-vita"
mkdir -p "$ROOT/sce_sys/livearea/contents"
cd "$ROOT/sce_sys"
curl -fsSL -o icon0.png \
  https://raw.githubusercontent.com/vitasdk/samples/master/hello_world/sce_sys/icon0.png
curl -fsSL -o livearea/contents/bg.png \
  https://raw.githubusercontent.com/vitasdk/samples/master/hello_world/sce_sys/livearea/contents/bg.png
curl -fsSL -o livearea/contents/startup.png \
  https://raw.githubusercontent.com/vitasdk/samples/master/hello_world/sce_sys/livearea/contents/startup.png
ls -la icon0.png livearea/contents/
sed -i 's/\r$//' "$ROOT/scripts/"*.sh
chmod +x "$ROOT/scripts/"*.sh
echo ASSETS_OK
