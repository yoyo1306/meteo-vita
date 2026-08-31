# Météo Vita

Homebrew météo pour PlayStation Vita (`.vpk`, install via VitaShell).

**Title ID :** `METV00001` · **Version :** voir [CHANGELOG.md](CHANGELOG.md)

## Fonctionnalités

- Météo actuelle : Météo-France Observations (station RADOME proche) avec repli Open-Meteo
- Prévisions horaires : Open-Meteo
- Recherche de ville (clavier système FR)
- UI style Sense (soleil / lune, pluie, neige, orage, etc.)
- Mode DEV : SELECT + L/R pour tester les scènes météo

## Prérequis PC

- WSL2 + Ubuntu
- VitaSDK (`/usr/local/vitasdk`) — `scripts/install-vitasdk.sh`

## Clé API Météo-France

```bash
cp src/mf_secret.h.example src/mf_secret.h
# Éditer MF_APIKEY_DEFAULT
```

Ou déposer la clé dans `ux0:data/meteo_vita/mf_apikey.txt` sur la Vita.

## Compiler

```bash
bash scripts/build.sh
```

Fichier à installer : `A_INSTALLER_MOI/Meteo_Vita__INSTALLER_MOI.vpk`

## Installer sur la Vita

1. USB ou FTP (VitaShell)
2. Copier le VPK vers `ux0:data/`
3. VitaShell → **Install**
4. Ouvrir la bulle **Météo Vita**
5. Après une mise à jour LiveArea : **désinstaller** puis réinstaller si la feuille ne se rafraîchit pas

## Releases

Les VPK publiés sont sur la page [Releases](../../releases) du dépôt GitHub.
