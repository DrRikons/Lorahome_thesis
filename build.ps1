param(
    [ValidateSet('GateWay', 'Sensor')]
    [string]$Mode = 'GateWay'
)

$ErrorActionPreference = 'Stop'
$bundleRoot = $PSScriptRoot
$pythonExecutable = Join-Path $bundleRoot '.platformio\python3\python.exe'

if (-not (Test-Path -LiteralPath $pythonExecutable)) {
    throw 'The bundled Windows Python runtime is missing.'
}

# Use only this bundle's runtime, libraries, platform and compiler packages.
$env:PYTHONHOME = Join-Path $bundleRoot '.platformio\python3'
$env:PYTHONPATH = Join-Path $bundleRoot '.platformio\core\site-packages'
$env:PYTHONNOUSERSITE = '1'
$env:PYTHONDONTWRITEBYTECODE = '1'
$env:PYTHONIOENCODING = 'utf-8'
$env:PLATFORMIO_CORE_DIR = Join-Path $bundleRoot '.platformio'
$env:PLATFORMIO_PACKAGES_DIR = Join-Path $bundleRoot '.platformio\packages'
$env:PLATFORMIO_PLATFORMS_DIR = Join-Path $bundleRoot '.platformio\platforms'
$env:PLATFORMIO_LIBDEPS_DIR = Join-Path $bundleRoot 'dependencies\libdeps'
$env:PLATFORMIO_SRC_DIR = Join-Path $bundleRoot "modes\$Mode"
$env:PLATFORMIO_BUILD_DIR = Join-Path $bundleRoot ".build\$Mode"
$env:PLATFORMIO_SETTING_ENABLE_TELEMETRY = 'No'
$env:PLATFORMIO_SETTING_CHECK_PLATFORMIO_INTERVAL = '36500'

& $pythonExecutable -m platformio run --project-dir $bundleRoot -e T3_V1_6_SX1276
exit $LASTEXITCODE
