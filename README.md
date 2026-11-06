# Ao Oni - Port para PS4 (GoldHEN)

Port no oficial de Ao Oni (RPG Maker) para PS4 Pro con GoldHEN, usando un runtime (EasyRPG Player o mkxp) compilado con OpenOrbis Toolchain.

## Estado
- [x] Identificar versión de RPG Maker del juego (RPG Maker XP / RGSS104E.dll)
- [x] Preparar entorno OpenOrbis (WSL Ubuntu + PkgTool de Windows)
- [x] Compilar y parchar ejemplo SDL2 para PS4 (`src/sdl2_test`)
- [x] Extractor de datos `.rxdata` (`scripts/extract_rxdata.py` y `scripts/prepare_aooni_assets.py`)
- [x] Primera versión funcional del motor `src/aooni_ps4` (pantalla de título, carga de los 76 mapas, renderizado de tilesets y sprites, colisiones y teletransportes entre habitaciones)

## Cómo compilar el PKG
```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Project aooni_ps4
```
El paquete resultante queda en `src/aooni_ps4/IV0000-AONI00001_00-AOONIPS4PORT0000.pkg`.

## Estructura
- `src/` codigo y parches
- `scripts/` scripts de build
- `pkg/` recursos del paquete (sin assets del juego)
- `docs/` notas

## Aviso
Este repo NO incluye archivos del juego. Debes usar tu propia copia legal en `game/` (ignorada por git).
