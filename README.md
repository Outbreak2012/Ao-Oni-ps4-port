# Ao Oni - Port para PS4 (GoldHEN)

Port no oficial de Ao Oni (RPG Maker) para PS4 Pro con GoldHEN, usando un runtime (EasyRPG Player o mkxp) compilado con OpenOrbis Toolchain.

## Estado
- [x] Identificar versión de RPG Maker del juego (RPG Maker XP / RGSS104E.dll)
- [x] Preparar entorno OpenOrbis (WSL Ubuntu + PkgTool de Windows)
- [x] Compilar y parchar ejemplo SDL2 para PS4 (`src/sdl2_test`)
- [x] Extractor completo de datos `.rxdata` (`scripts/extract_rxdata.py` y `scripts/prepare_aooni_assets.py`)
- [x] Intérprete nativo 1:1 de eventos de RPG Maker XP (`src/aooni_ps4/aooni/main.cpp`)
- [x] Renderizado con paleta nativa `A8B8G8R8Srgb` de PS4, capas de mapa pre-renderizadas en caché y tabla de 48 sub-patrones de Autotiles
- [x] Movimiento fluido sub-tile a 60 FPS, transiciones suaves con fundido a negro (`fade out` / `fade in`) y tonos de iluminación de pantalla (`cmd 223`)
- [x] Motor de audio multicanal por hardware (`sceAudioOut` a 48 kHz estéreo) para BGM, BGS y efectos de sonido (SE) con soporte de pitch y volumen
- [x] Soporte completo de puzzles, inventario, cajas fuertes (`cmd 103`), escondites en armarios (`cmd 105`), persecuciones de Ao Oni (`CommonEvents`) y guardado rápido

## Controles en DualShock 4
- **Cruceta / Stick Izquierdo**: Mover a Hiroshi / navegar menús y dígitos de cajas fuertes
- **X (Cruz)**: Interactuar / avanzar diálogos / confirmar elección o código
- **Círculo / Triángulo**: Abrir o cerrar inventario de objetos
- **Cuadrado** *(dentro del inventario)*: Guardar partida rápidamente
- **OPTIONS**: Volver a la pantalla de título

## Cómo preparar los assets y compilar el PKG
```powershell
python .\scripts\prepare_aooni_assets.py
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
