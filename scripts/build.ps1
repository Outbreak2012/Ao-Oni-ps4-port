# Uso: .\scripts\build.ps1 -Project hello_test
#      .\scripts\build.ps1 -Project sdl2_test
# Compila src/<Project> en WSL (Ubuntu) y empaqueta el .pkg con PkgTool de Windows.
# Requisitos: WSL Ubuntu con clang, lld y make; PS4Toolchain/ en la raiz del proyecto.
param([Parameter(Mandatory=$true)][string]$Project)

$root = Split-Path -Parent $PSScriptRoot
$tc   = Join-Path $root "PS4Toolchain"
$src  = Join-Path $root "src\$Project"
if (-not (Test-Path $src)) { throw "No existe $src" }
$wslRoot = "/mnt/" + $root.Substring(0,1).ToLower() + ($root.Substring(2) -replace '\\','/')

# 1) Compilar y generar eboot.bin, param.sfo y pkg.gp4 dentro de WSL.
#    (El empaquetado final falla en Linux por OpenSSL 3, por eso se hace en Windows.)
wsl -d Ubuntu --cd "$wslRoot/src/$Project" -e env "OO_PS4_TOOLCHAIN=$wslRoot/PS4Toolchain" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 make eboot.bin sce_sys/param.sfo pkg.gp4
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion en WSL" }

# 2) Empaquetar con PkgTool de Windows.
Push-Location $src
& "$tc\bin\windows\PkgTool.Core.exe" pkg_build pkg.gp4 .
Pop-Location
Get-ChildItem $src -Filter *.pkg | Select-Object Name, Length
</Content>
