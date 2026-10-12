param(
    [string]$CubeFwRoot = (Join-Path $env:USERPROFILE 'STM32Cube/Repository/STM32Cube_FW_H5_V1.6.0'),
    [ValidateSet('led_direct', 'pulse_distributor')]
    [string]$Example = 'led_direct'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$projectDir = Join-Path $repo 'H503_Peripheral_DBGer/MDK-ARM'
$projectFile = Join-Path $projectDir 'H503_Peripheral_DBGer.uvprojx'
$template = Join-Path $CubeFwRoot 'Projects/NUCLEO-H503RB/Templates/STM32CubeIDE'
$startup = Join-Path $template 'Example/Startup/startup_stm32h503rbtx.s'
$linker = Join-Path $template 'STM32H503RBTX_FLASH.ld'
$name = "H503_TPLD_${Example}_10Hz"
$elf = Join-Path $repo "tmp/$name.elf"
$hex = Join-Path $repo "firmware/$name.hex"
$exampleDefine = if ($Example -eq 'pulse_distributor') { @('-DTPLD_PULSE_DISTRIBUTOR') } else { @() }

foreach ($path in @($projectFile, $startup, $linker)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing build input: $path" }
}
if (-not (Get-Command arm-none-eabi-gcc -ErrorAction SilentlyContinue)) {
    throw 'arm-none-eabi-gcc is required on PATH'
}

[xml]$project = Get-Content -LiteralPath $projectFile
$sources = @($project.Project.Targets.Target.Groups.Group.Files.File |
    Where-Object { $_.FilePath -like '*.c' } |
    ForEach-Object { [IO.Path]::GetFullPath((Join-Path $projectDir $_.FilePath)) })
$includeDirs = @(
    'Core/Inc',
    'Drivers/STM32H5xx_HAL_Driver/Inc',
    'Drivers/STM32H5xx_HAL_Driver/Inc/Legacy',
    'Drivers/CMSIS/Device/ST/STM32H5xx/Include',
    'Drivers/CMSIS/Include'
) | ForEach-Object { '-I' + (Join-Path (Join-Path $repo 'H503_Peripheral_DBGer') $_) }

New-Item -ItemType Directory -Force (Split-Path $elf), (Split-Path $hex) | Out-Null
& arm-none-eabi-gcc -mcpu=cortex-m33 -mthumb -mfloat-abi=soft -Os -g3 `
    -ffunction-sections -fdata-sections -DSTM32H503xx -DUSE_HAL_DRIVER $exampleDefine `
    $includeDirs '-Wl,--gc-sections' '-specs=nosys.specs' '-T' $linker `
    $startup $sources -o $elf
if ($LASTEXITCODE -ne 0) { throw 'GCC link failed' }

& arm-none-eabi-objcopy -O ihex $elf $hex
if ($LASTEXITCODE -ne 0) { throw 'HEX conversion failed' }
& arm-none-eabi-size $elf
Write-Output "Wrote $hex"
