# Compilation de Tuneforge avec MSVC.
#   .\build.ps1              -> Release
#   .\build.ps1 -Config Debug
#   .\build.ps1 -Clean

param(
    [ValidateSet('Release', 'Debug')]
    [string]$Config = 'Release',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$root  = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path $root 'build'

if ($Clean -and (Test-Path $build)) {
    Write-Host 'Nettoyage du dossier build...' -ForegroundColor Yellow
    Remove-Item $build -Recurse -Force
}

# Dear ImGui est un sous-module. Un « git clone » sans --recursive laisse le
# dossier vide, et l'erreur CMake qui suit n'explique rien : on repare avant.
$imgui = Join-Path $root 'third_party\imgui\imgui.cpp'
if (-not (Test-Path $imgui)) {
    if (Test-Path (Join-Path $root '.git')) {
        Write-Host 'Recuperation du sous-module Dear ImGui...' -ForegroundColor Yellow
        & git -C $root submodule update --init --depth 1 third_party/imgui
        if ($LASTEXITCODE -ne 0) { throw 'Echec de la recuperation du sous-module.' }
    } else {
        throw "third_party\imgui est vide et le dossier n'est pas un depot git. " +
              'Clonez avec : git clone --recursive <url>'
    }
}

# Localise CMake (winget l'installe hors PATH pour la session courante).
$cmake = 'cmake'
try { Get-Command cmake -ErrorAction Stop | Out-Null } catch {
    $candidate = "$env:ProgramFiles\CMake\bin\cmake.exe"
    if (Test-Path $candidate) { $cmake = $candidate }
    else { throw 'CMake introuvable. Installez-le : winget install Kitware.CMake' }
}

# Localise le toolchain MSVC via vswhere.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    throw 'Visual Studio Build Tools introuvable. Installez-les : winget install Microsoft.VisualStudio.2022.BuildTools'
}
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'Composant VC++ absent de l''installation Visual Studio.' }

Write-Host "Toolchain : $vsPath" -ForegroundColor DarkGray
Write-Host "Configuration : $Config" -ForegroundColor Cyan

& $cmake -S $root -B $build -G 'Visual Studio 17 2022' -A x64
if ($LASTEXITCODE -ne 0) { throw 'Echec de la configuration CMake.' }

& $cmake --build $build --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw 'Echec de la compilation.' }

$bin = Join-Path $build 'bin'
Write-Host ''
Write-Host 'Compilation terminee.' -ForegroundColor Green
Get-ChildItem $bin -Filter *.exe | ForEach-Object {
    Write-Host ("  {0}  ({1:N0} Ko)" -f $_.Name, ($_.Length / 1KB))
}
Write-Host ''
Write-Host 'Essayez :' -ForegroundColor Cyan
Write-Host "  $bin\tuneforge.exe detect"
Write-Host "  $bin\tuneforge.exe list"
Write-Host "  $bin\tuneforge.exe apply equilibre --dry-run"
