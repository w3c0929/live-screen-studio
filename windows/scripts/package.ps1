param(
    [string]$Version = "0.8.1",
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
$ocrOut = Join-Path $dist "ocr"
if (Test-Path $ocrOut) { Remove-Item $ocrOut -Recurse -Force }
dotnet publish (Join-Path $root "poc\ocr_poc\ocr_poc.csproj") -c Release -r win-x64 `
    --self-contained true -p:PublishSingleFile=true `
    -p:IncludeNativeLibrariesForSelfExtract=true `
    -p:EnableCompressionInSingleFile=true -p:PublishTrimmed=false `
    -p:DebugType=None -p:DebugSymbols=false -o $ocrOut

$stage = Join-Path $dist "LiveZoom-$Version-windows-x64-portable"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null

Copy-Item $exe $stage
Copy-Item (Join-Path $root "README.md") $stage
New-Item -ItemType Directory -Force (Join-Path $stage "ocr") | Out-Null
Copy-Item (Join-Path $ocrOut "ocr_poc.exe") (Join-Path $stage "ocr")

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
