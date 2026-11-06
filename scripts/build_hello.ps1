# Compila src/hello_test en WSL (Ubuntu) y empaqueta el .pkg con la herramienta de Windows.
# Requisitos: WSL Ubuntu con clang, lld y make; PS4Toolchain/ en la raiz del proyecto.
$root = Split-Path -Parent $PSScriptRoot
$tc   = Join-Path $root "PS4Toolchain"
$src  = Join-Path $root "src\hello_test"
$wslRoot = "/mnt/" + $root.Substring(0,1).ToLower() + ($root.Substring(2) -replace '\\','/')

# 1) Compilar y generar eboot.bin dentro de WSL (el empaquetado falla en Linux y se ignora).
wsl -d Ubuntu --cd "$wslRoot/src/hello_test" -e env "OO_PS4_TOOLCHAIN=$wslRoot/PS4Toolchain" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 make eboot.bin sce_sys/param.sfo pkg.gp4

# 2) Empaquetar con PkgTool de Windows.
Push-Location $src
& "$tc\bin\windows\PkgTool.Core.exe" pkg_build pkg.gp4 .
Pop-Location
Get-ChildItem $src -Filter *.pkg | Select-Object Name, Length
