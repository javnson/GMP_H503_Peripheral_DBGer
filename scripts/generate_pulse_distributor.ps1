param(
    [string]$InterConnectRoot = 'C:\ti\interconnectstudio_1.8.0'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cli = Join-Path $InterConnectRoot 'sysconfig_cli.bat'
$product = Join-Path $InterConnectRoot 'product/.metadata/product.json'
$syscfg = Join-Path $repo 'cpld/pulse_distributor/pulse_distributor.syscfg'
$generatedDir = Join-Path $repo 'tmp/pulse_distributor'
$hex = Join-Path $repo 'cpld/pulse_distributor/program.hex'
$header = Join-Path $repo 'H503_Peripheral_DBGer/Core/Inc/tpld2001_pulse_distributor_image.h'

foreach ($path in @($cli, $product, $syscfg)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing input: $path" }
}
New-Item -ItemType Directory -Force $generatedDir | Out-Null
& $cli --product $product --script $syscfg --output $generatedDir
if ($LASTEXITCODE -ne 0) { throw 'InterConnect Studio generation failed' }
Copy-Item -LiteralPath (Join-Path $generatedDir 'program.hex') -Destination $hex
& python (Join-Path $repo 'scripts/generate_tpld_image.py') $hex --output $header
if ($LASTEXITCODE -ne 0) { throw 'TPLD image conversion failed' }
