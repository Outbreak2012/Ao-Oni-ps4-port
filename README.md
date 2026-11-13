# Ao Oni — Port 1:1 para PS4 / PS4 Pro (GoldHEN)

Port casero nativo en C++17 de **Ao Oni** (RPG Maker XP / RGSS1) para **PlayStation 4** y **PS4 Pro** con GoldHEN, desarrollado con **OpenOrbis PS4 Toolchain**, **SDL2** y salida nativa de audio/vídeo por hardware (`sceAudioOut` y `sceVideoOut`).

> **Autor del port para PS4:** **[Outbreak2012](https://github.com/Outbreak2012/Ao-Oni-ps4-port)**  
> **Título / ID:** `Ao Oni PS4` (`IV0000-AONI00001_00-AOONIPS4PORT0000`)

---

## Características principales

- **Motor nativo RGSS1 1:1 en C++ (`src/aooni_ps4/aooni/main.cpp`)**  
  Reimplementa el intérprete de eventos, rutas de movimiento (`cmd 205` / `cmd 209`, incluyendo saltos parabólicos `Jump`), clima de lluvia (`cmd 236`), rotación de imágenes (`cmd 233`), animaciones (`cmd 207` `bang.png`), variables, interruptores y colisiones de RPG Maker XP sin dependencias externas de Ruby.
- **Bloqueado a 60 FPS fluidos con sincronización VSync nativa**  
  Presentación sincronizada directamente con `sceVideoOutSubmitFlip` (`VSYNC`) evitando esperas dobles de VSync que reducían a 30 FPS, filtro de tono mediante tabla **LUT de 256 entradas**, caché directo de superficies `SDL_Surface*` por evento/imagen y limpieza única de bordes negros en VRAM al cambiar de modo de vídeo.
- **Vídeo PS4 / PS4 Pro hasta 4K Nativo**  
  Renderizado base fiel `640x480` escalado por hardware a **720p**, **1080p** o **4K Nativo (`3840x2160`)** mediante `sceVideoOut` con copia desenrollada en scanlines y relación de aspecto configurable (**4:3**, **16:9** o **Pixel-Perfect**).
- **Correcciones gráficas y de escenarios 1:1**
  - **Interfaz fiel con Windowskin (`ws_wskin_n.png`)**: cuadros de diálogo, elecciones, menú de título, teclado numérico, pantalla de nombre e inventario paginado renderizados con el marco original.
  - **Detección visual de objetos que pasaban desapercibidos**: los 42 ítems cuentan con su icono RTP completo (incluyendo `031-Key03` para llaves y `023-Potion03` para Vinagre), mejora de contraste en sprites minúsculos/oscuros (`nejiana`, `oil`, `bou`), brillo vivo unificado en llaves/cajones/objetos recogibles y un **indicador contextual `[X]`** frente a elementos interactuables (alternable en cualquier momento con **TOUCHPAD**).
  - **Puertas y rejas de celdas transparentes (`char_173-Door04.png`)**: barrotes metálicos con fondo transparente en `Map 012`, `018`, `039`, `044` y `068` para ver el interior de las celdas.
  - **Enmascarado de salas ocultas fuera de límites y bordes de autotiles**: oculta las celdas auxiliares (`AT-hekitainai`) y los *Ao Oni* estacionados en ellas, recomputando las esquinas de `Slot 1` (`48..95`) con la tabla oficial de 48 sub-patrones de RPG Maker XP.
  - **Corrección de sustracción lineal de tonos (`applyToneRectFast`)**: aplicación exacta por canal (`clamp(c + tone, 0, 255)`) para tonos negativos como `(-85, -85, -85)` en interiores y epílogo.
  - **Limpieza automática de microporos negros en tilesets (`room-01-*` y `003-Forest01`)**: rellena píxeles transparentes internos accidentales sin alterar rejas metálicas ni barrotes.
  - **Sprites de epílogo, interruptor y cuervos**: sprite propio de tendero/peatón para el epílogo (`char_148-Storekeeper02.png`), interruptor eléctrico animado de 4 estados (`char_179-Switch02.png`), y cuervos posados/volando (`char_166-Small08.png` / `char_165-Small07.png`).
  - **Orden Z correcto (`always_top`)** sobre la capa superior.
- **Audio multicanal (`sceAudioOut` 48 kHz estéreo) y BGS ambientales**
  - Reproducción simultánea de **BGM**, **BGS**, **ME** y **SE** con control de volumen, pitch y fundidos (`fadeIn` / `fadeOut`).
  - Generación de bucles ambientales sin cortes para los BGS RTP faltantes (`001-Wind01`, `003-Wind03`, `005-Rain01`, `006-Rain02`, `015-Quake01`, `017-Creature01`).
  - Soporte de `@autoplay_bgs` y `@bgs` por mapa para detener automáticamente `017-Creature01` al salir de armarios o del cuarto de los Ao Onis (`Map 068`).
- **Calidad de vida (QoL) en PS4**
  - Auto-guardado de punto de control al cambiar a salas seguras y **guardado rápido** en cualquier zona segura.
  - Teclado numérico compacto para cajas fuertes y candados (`cmd 103`).
  - Retardo de seguridad justo en apariciones del Ao Oni y ajuste equilibrado en persecuciones y escondites.
  - Retroalimentación háptica y barra LED del **DualShock 4** durante persecuciones.

---

## Controles (DualShock 4)

| Botón | Acción |
| --- | --- |
| **Cruceta / Stick Izquierdo** | Mover a Hiroshi / navegar menús y dígitos |
| **X (Cruz)** | Interactuar / confirmar / avanzar diálogo |
| **Círculo** | Cancelar / volver |
| **Cuadrado** | Guardado rápido *(en mapa seguro o inventario)* |
| **Triángulo / OPTIONS** | Abrir o cerrar inventario |
| **L1 / R1** | Correr en el mapa / avance rápido de diálogo |
| **TOUCHPAD** | Activar/desactivar brillo de objetos e indicador `[X]` |
| **R2** | Cambiar resolución de salida (`720p` / `1080p` / `4K Nativo`) |
| **L2** | Cambiar relación de aspecto (`4:3` / `16:9` / `Pixel-Perfect`) |
| **L1 + R1 + OPTIONS** | Volver a la pantalla de título |

---

## Requisitos y compilación

El repositorio **no distribuye los archivos originales ni binarios `.pkg` del juego** (`Ao Oni/` está ignorado en `.gitignore`). Necesitas colocar tu copia de **Ao Oni** en la carpeta `Ao Oni/` y contar con `PS4Toolchain/` en la raíz del proyecto, **WSL (Ubuntu)** con `clang`/`lld`/`make`/`python3`, y **Windows** para ejecutar `PkgTool.Core.exe`.

1. **Preparar los assets nativos de PS4** (mapas `.bin`/`.json`, imágenes parcheadas y audio PCM 48 kHz):
   ```powershell
   wsl -d Ubuntu -e python3 /mnt/<ruta-al-repo>/scripts/prepare_aooni_assets.py
   ```
   *(También se ejecuta automáticamente desde `build.ps1` si falta `src/aooni_ps4/assets/misc/map_001.bin`.)*

2. **Compilar `eboot.bin` y empaquetar el `.pkg`**:
   ```powershell
   powershell -ExecutionPolicy Bypass -File .\scripts\build.ps1 -Project aooni_ps4
   ```
   El paquete generado queda en:
   - `src/aooni_ps4/IV0000-AONI00001_00-AOONIPS4PORT0000.pkg`

---

## Estructura del proyecto

- `src/aooni_ps4/` — Código fuente del port (`aooni/main.cpp`), `Makefile`, recursos `sce_sys/` y empaquetado PS4.
- `scripts/extract_rxdata.py` — Parser de archivos Marshal/Ruby `.rxdata` de RPG Maker XP en Python puro.
- `scripts/prepare_aooni_assets.py` — Exportador de mapas, base de datos, audio PCM 48 kHz y generador/parcheador de sprites y tilesets.
- `scripts/build.ps1` — Script de compilación en WSL (OpenOrbis) y creación del `.pkg` con `PkgTool.Core.exe`.
- `src/hello_test/`, `src/sdl2_test/` — Proyectos base de prueba para OpenOrbis y SDL2 en PS4.
- `docs/`, `pkg/` — Notas técnicas y recursos auxiliares.

---

## Contribuciones

Las mejoras de rendimiento, correcciones de compatibilidad o ajustes de interfaz son bienvenidas:

1. Haz un *fork* del repositorio [`Outbreak2012/Ao-Oni-ps4-port`](https://github.com/Outbreak2012/Ao-Oni-ps4-port).
2. Crea una rama descriptiva para tu cambio (`git checkout -b fix/mi-mejora`).
3. Verifica la compilación y las pruebas antes de enviar un *Pull Request*.

---

## Créditos y atribuciones

- **Creador original de Ao Oni:** **noprops** (desarrollado originalmente en RPG Maker XP).
- **Traducción al inglés (referencia en `Ao Oni/Léeme.txt`):** [mygames888.info/aooni.html](http://mygames888.info/aooni.html)
- **Traducción al español (referencia en `Ao Oni/Léeme.txt`):** Traducción incluida en `Ao Oni/Léeme.txt` ([perfil en Facebook](https://www.facebook.com/profile.php?id=100005980818932)).
- **Port nativo para PS4 / PS4 Pro y mantenimiento:** **[Outbreak2012](https://github.com/Outbreak2012/Ao-Oni-ps4-port)**.
- **Herramientas y librerías de terceros:** **OpenOrbis PS4 Toolchain**, **SDL2** y **stb_image**.

> **Aviso legal:** *Ao Oni* y sus recursos originales pertenecen a **noprops** y a sus respectivos autores. Este proyecto es un port no oficial sin fines de lucro realizado por fans para preservar y jugar tu propia copia legal en PlayStation 4.
