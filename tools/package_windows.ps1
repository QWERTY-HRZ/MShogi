param(
    [Parameter(Mandatory = $true)][string]$BuildDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][string]$QtRoot,
    [Parameter(Mandatory = $true)][string]$OnnxRuntimeDll,
    [Parameter(Mandatory = $true)][string]$OnnxModel
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$build = [System.IO.Path]::GetFullPath($BuildDirectory)
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
$qt = [System.IO.Path]::GetFullPath($QtRoot)
$runtime = [System.IO.Path]::GetFullPath($OnnxRuntimeDll)
$model = [System.IO.Path]::GetFullPath($OnnxModel)
$app = Join-Path $build 'src\MShogiApp.exe'
$modelManifest = Join-Path (Split-Path $model -Parent) 'manifest.json'
$windeployqt = Join-Path $qt 'bin\windeployqt.exe'
$offscreenPlugin = Join-Path $qt 'plugins\platforms\qoffscreen.dll'

foreach ($path in @($app, $runtime, $model, $modelManifest, $windeployqt,
                     $offscreenPlugin)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required deployment input is missing: $path"
    }
}
if (Test-Path -LiteralPath $output) {
    throw "Output directory already exists: $output"
}

$manifestData = Get-Content -LiteralPath $modelManifest -Raw | ConvertFrom-Json
$actualModelHash = (Get-FileHash -LiteralPath $model -Algorithm SHA256).Hash
if ($actualModelHash -ne $manifestData.onnx_sha256) {
    throw 'The model SHA-256 does not match its manifest.'
}

New-Item -ItemType Directory -Path $output | Out-Null
New-Item -ItemType Directory -Path (Join-Path $output 'models') | Out-Null
Copy-Item -LiteralPath $app -Destination (Join-Path $output 'MShogiApp.exe')
Copy-Item -LiteralPath $runtime -Destination (Join-Path $output 'onnxruntime.dll')
Copy-Item -LiteralPath $model -Destination (
    Join-Path $output 'models\mshogi_policy_value.onnx')
Copy-Item -LiteralPath $modelManifest -Destination (Join-Path $output 'models\manifest.json')

& $windeployqt --release --no-translations --compiler-runtime (
    Join-Path $output 'MShogiApp.exe')
if ($LASTEXITCODE -ne 0) {
    throw "windeployqt failed with exit code $LASTEXITCODE"
}

# ONNX Runtime 官方 Windows DLL 依赖 MSVC 运行库，随包携带以免用户另装。
foreach ($name in @('msvcp140.dll', 'msvcp140_1.dll', 'vcruntime140.dll',
                     'vcruntime140_1.dll')) {
    $source = Join-Path $env:SystemRoot "System32\$name"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "MSVC runtime is missing: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $output $name)
}
Copy-Item -LiteralPath $offscreenPlugin -Destination (
    Join-Path $output 'platforms\qoffscreen.dll')

$files = Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName
$deployment = [ordered]@{
    manifest_version = 1
    source_commit = (git -C (Split-Path $PSScriptRoot -Parent) rev-parse HEAD).Trim()
    model_sha256 = $actualModelHash
    files = @($files | ForEach-Object {
        [ordered]@{
            path = [System.IO.Path]::GetRelativePath($output, $_.FullName).Replace('\', '/')
            bytes = $_.Length
            sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash
        }
    })
    total_bytes = ($files | Measure-Object Length -Sum).Sum
}
$deployment | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (
    Join-Path $output 'deployment_manifest.json') -Encoding utf8
Write-Output ($deployment | ConvertTo-Json -Depth 5)
