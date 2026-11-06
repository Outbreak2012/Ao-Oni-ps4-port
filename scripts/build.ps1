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

# Si el proyecto es aooni_ps4, asegurar que sce_module, sce_sys y assets esten preparados
if ($Project -eq "aooni_ps4") {
    if (-not (Test-Path "$src\sce_module")) {
        robocopy "$root\src\sdl2_test\sce_module" "$src\sce_module" /E | Out-Null
    }
    if (-not (Test-Path "$src\sce_sys")) {
        robocopy "$root\src\sdl2_test\sce_sys" "$src\sce_sys" /E | Out-Null
    }
    if (-not (Test-Path "$src\assets\misc\map_001.bin")) {
        wsl -d Ubuntu -e python3 "$wslRoot/scripts/prepare_aooni_assets.py"
    }
}

# 1) Compilar y generar eboot.bin, param.sfo y pkg.gp4 dentro de WSL.
#    (El empaquetado final falla en Linux por OpenSSL 3, por eso se hace en Windows.)
wsl -d Ubuntu --cd "$wslRoot/src/$Project" -e env "OO_PS4_TOOLCHAIN=$wslRoot/PS4Toolchain" DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1 make eboot.bin sce_sys/param.sfo pkg.gp4
if ($LASTEXITCODE -ne 0) { throw "Fallo la compilacion en WSL" }

# 2) Empaquetar con PkgTool de Windows.
Push-Location $src
& "$tc\bin\windows\PkgTool.Core.exe" pkg_build pkg.gp4 .
Pop-Location
Get-ChildItem $src -Filter *.pkg | Select-Object Name, Length

