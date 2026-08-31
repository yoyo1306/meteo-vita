# Changelog

Toutes les versions notables de **Météo Vita** (Title ID `METV00001`).

Le format s’inspire de [Keep a Changelog](https://keepachangelog.com/fr/1.1.0/).
La numérotation suit `VITA_VERSION` (`XX.YY`).

## [00.45] — 2026-08-13

### Ajouté
- Mode DEV (SELECT) : scènes météo fictives, L/R pour défiler
- LiveArea : `Ver. XX.YY` visible en bas à gauche de la feuille
- Bouton refresh orange (flèches blanches), hauteur alignée sur la pastille ville
- Obs. actuelles via Météo-France (station RADOME) + prévisions Open-Meteo
- Recherche de ville (IME AZERTY) + liste de résultats
- Sons de clic UI

### Corrigé
- Icône refresh lisible en 960×544 (traits épais, plus de PNG flou)
- Charge GPU du refresh (évite crash en spam L/R mode DEV)
- Boucle IME / sélection de ville (crash GPU)
- Éclairs positionnés sous les nuages

### Technique
- Build WSL + VitaSDK : `bash scripts/build.sh`
- VPK : `A_INSTALLER_MOI/Meteo_Vita__INSTALLER_MOI.vpk`
- Clé MF : `src/mf_secret.h` (gitignoré) ou `ux0:data/meteo_vita/mf_apikey.txt`

## [00.01] — 2026-08-12

### Ajouté
- Première UI (layout croquis, données fictives)
- Packaging VPK + LiveArea de base
