# Notas de pruebas

## hello_test (OpenOrbis hello_world)
- Compila y empaqueta OK. En shadPS4 arranca y queda en negro (no dibuja nada, es lo esperado).

## sdl2_test (ejemplo SDL2 de OpenOrbis)
- Compila y empaqueta OK con scripts/build.ps1 -Project sdl2_test.
- shadPS4: llega a "Entering draw loop" y se cierra (0xc0000005). Causa: shadPS4 no emula
  libSceFreeTypeOl (modulo de fuentes del firmware); FT_* devuelven 0 falso y el ejemplo dibuja con puntero nulo.
- Pendiente: probar en PS4 real (GoldHEN 11.00), donde ese modulo si existe.

## Build
- Codigo: WSL Ubuntu (clang/lld/make). Empaquetado: PkgTool.Core.exe de Windows
  (la version de Linux necesita OpenSSL 1.x y Ubuntu 26.04 solo trae 3.x).
- Uso: .\scripts\build.ps1 -Project hello_test | sdl2_test
