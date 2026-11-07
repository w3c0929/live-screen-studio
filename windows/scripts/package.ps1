param(
    [string]$Version = "0.3.0",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root "build-release"
$dist = Join-Path $root "dist"

if (-not $SkipBuild) {
    cmake -S $root -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build $build -j
    ctest --test-dir $build --output-on-failure
}

$exe = Join-Path $build "src\app\livezoom_win.exe"
if (-not (Test-Path $exe)) {
    throw "livezoom_win.exe not found; build first"
}

New-Item -ItemType Directory -Force $dist | Out-Null
$stage = Join-Path $dist "LiveZoom-$Version-windows-x64-portable"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exe $stage
Copy-Item (Join-Path $root "README.md") $stage

$zip = Join-Path $dist "LiveZoom-$Version-windows-x64-portable.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path (Join-Path $stage "*") -DestinationPath $zip -Force
Remove-Item $stage -Recurse -Force

$iscc = Get-Command iscc -ErrorAction SilentlyContinue
if ($iscc) {
    & $iscc.Source (Join-Path $root "assets\livezoom.iss")
} else {
    Write-Warning "iscc (Inno Setup) not found; skipping installer"
}

$setup = Join-Path $dist "LiveZoom-$Version-windows-x64-setup.exe"
$files = @($zip)
if (Test-Path $setup) { $files += $setup }

$sums = Join-Path $dist "SHA256SUMS.txt"
$lines = foreach ($file in $files) {
    $hash = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLowerInvariant()
    "$hash  $(Split-Path -Leaf $file)"
}
$lines | Set-Content -Encoding ASCII $sums

Write-Output "Built:"
$files | ForEach-Object { Write-Output "  $_" }
Write-Output "  $sums"
