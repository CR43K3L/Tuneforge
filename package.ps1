# Construit l'installateur Tuneforge.
#   .\package.ps1             -> compile en Release, puis produit dist\Tuneforge-<version>-setup.exe
#   .\package.ps1 -SkipBuild  -> reutilise build\bin tel quel

param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

# Une seule source pour la version : CMakeLists.txt. L'installateur, la carte
# « A propos » et les proprietes des executables affichent donc la meme.
$cmake = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch 'project\(Tuneforge VERSION ([0-9]+\.[0-9]+\.[0-9]+)') {
    throw 'Version introuvable dans CMakeLists.txt.'
}
$version = $Matches[1]

if (-not $SkipBuild) {
    & (Join-Path $root 'build.ps1')
}

$bin = Join-Path $root 'build\bin'
foreach ($exe in 'tuneforge-gui.exe', 'tuneforge.exe', 'tuneforge-reset.exe') {
    if (-not (Test-Path (Join-Path $bin $exe))) {
        throw "$exe absent de build\bin : compilez d'abord (.\build.ps1)."
    }
}

# Inno Setup : dans le PATH, ou dans l'un de ses emplacements d'installation.
$iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    $iscc = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    ) | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $iscc) {
    throw 'Inno Setup 6 introuvable. Installez-le : winget install JRSoftware.InnoSetup'
}

Write-Host "Installateur : Tuneforge $version" -ForegroundColor Cyan
& $iscc /Qp "/DAppVersion=$version" "/DBinDir=$bin" (Join-Path $root 'installer\tuneforge.iss')
if ($LASTEXITCODE -ne 0) { throw "Echec de la compilation de l'installateur." }

$out = Join-Path $root "dist\Tuneforge-$version-setup.exe"
$hash = (Get-FileHash $out -Algorithm SHA256).Hash

Write-Host ''
Write-Host 'Installateur pret.' -ForegroundColor Green
Write-Host ("  {0}  ({1:N0} Ko)" -f $out, ((Get-Item $out).Length / 1KB))
# L'empreinte permet a qui le recoit de verifier que le fichier n'a pas ete
# altere en route. Elle ne remplace pas une signature, mais elle ne coute rien.
Write-Host "  SHA-256 : $hash"
