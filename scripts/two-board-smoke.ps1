param(
    [Parameter(Mandatory = $true)][string]$MasterPort,
    [Parameter(Mandatory = $true)][string]$TargetPort,
    [ValidateRange(1, 100)][int]$Iterations = 3,
    [string]$CliPath = (Join-Path $PSScriptRoot '..\host\dist\bin\gmpctl.exe')
)

$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath $CliPath)) { throw "Missing CLI: $CliPath" }

function Invoke-Bridge([string]$Port, [string[]]$Arguments) {
    $output = & $CliPath $Port @Arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "$Port $($Arguments -join ' '): $($output -join ' ')"
    }
    return ($output -join "`n").Trim()
}

function Assert-Bridge([string]$Actual, [string]$Expected, [string]$Label) {
    if ($Actual -ne $Expected) {
        throw "$Label expected '$Expected', got '$Actual'"
    }
}

Assert-Bridge (Invoke-Bridge $MasterPort @('info')).Split(' protocol ')[0] 'GMP Peripheral ISO Host' 'master identity'
Assert-Bridge (Invoke-Bridge $TargetPort @('info')).Split(' protocol ')[0] 'GMP Peripheral ISO Host' 'target identity'

foreach ($bus in @('uart', 'rs232', 'rs485')) {
    Invoke-Bridge $TargetPort @('reg', $bus, 'set', '0x10', 'AABBCC') | Out-Null
    Invoke-Bridge $TargetPort @($bus, 'config', 'slave', '115200') | Out-Null
    Invoke-Bridge $MasterPort @($bus, 'config', 'stream', '115200') | Out-Null
    for ($i = 0; $i -lt $Iterations; ++$i) {
        Assert-Bridge (Invoke-Bridge $MasterPort @('remote', $bus, '0', 'get', '0x10', '3')) 'aabbcc' "$bus read $i"
        Invoke-Bridge $MasterPort @('remote', $bus, '0', 'set', '0x10', '112233') | Out-Null
        Assert-Bridge (Invoke-Bridge $TargetPort @('reg', $bus, 'get', '0x10', '3')) '112233' "$bus write $i"
        Invoke-Bridge $TargetPort @('reg', $bus, 'set', '0x10', 'AABBCC') | Out-Null
    }
    Write-Output "$bus PASS ($Iterations rounds)"
}

Invoke-Bridge $TargetPort @('reg', 'i2c', 'set', '0x10', 'AABBCC') | Out-Null
Invoke-Bridge $TargetPort @('i2c-target', '0x42') | Out-Null
try {
    for ($i = 0; $i -lt $Iterations; ++$i) {
        Assert-Bridge (Invoke-Bridge $MasterPort @('remote', 'i2c', '0x42', 'get', '0x10', '3')) 'aabbcc' "i2c read $i"
        Invoke-Bridge $MasterPort @('remote', 'i2c', '0x42', 'set', '0x10', '112233') | Out-Null
        Assert-Bridge (Invoke-Bridge $TargetPort @('reg', 'i2c', 'get', '0x10', '3')) '112233' "i2c write $i"
        Invoke-Bridge $TargetPort @('reg', 'i2c', 'set', '0x10', 'AABBCC') | Out-Null
    }
    Write-Output "i2c PASS ($Iterations rounds)"
} finally {
    Invoke-Bridge $TargetPort @('i2c-target', 'off') | Out-Null
}

foreach ($mode in 0..3) {
    Invoke-Bridge $MasterPort @('spi', 'config', "$mode", '7') | Out-Null
    Invoke-Bridge $TargetPort @('spi', 'target', "$mode") | Out-Null
    Invoke-Bridge $TargetPort @('reg', 'spi', 'set', '0x10', 'AABBCC') | Out-Null
    for ($i = 0; $i -lt $Iterations; ++$i) {
        Assert-Bridge (Invoke-Bridge $MasterPort @('remote', 'spi', '0', 'get', '0x10', '3')) 'aabbcc' "spi mode $mode read $i"
        Invoke-Bridge $MasterPort @('remote', 'spi', '0', 'set', '0x10', '112233') | Out-Null
        Assert-Bridge (Invoke-Bridge $TargetPort @('reg', 'spi', 'get', '0x10', '3')) '112233' "spi mode $mode write $i"
        Invoke-Bridge $TargetPort @('reg', 'spi', 'set', '0x10', 'AABBCC') | Out-Null
    }
    Write-Output "spi mode $mode PASS ($Iterations rounds)"
}

Invoke-Bridge $TargetPort @('reg', 'i3c', 'set', '0x10', 'AABBCC') | Out-Null
Invoke-Bridge $TargetPort @('i3c', 'target') | Out-Null
Invoke-Bridge $MasterPort @('i3c', 'controller') | Out-Null
Assert-Bridge (Invoke-Bridge $MasterPort @('i3c', 'daa', '0x30', '1')) '0x30' 'i3c dynamic address'
for ($i = 0; $i -lt $Iterations; ++$i) {
    Assert-Bridge (Invoke-Bridge $MasterPort @('remote', 'i3c', '0x30', 'get', '0x10', '3')) 'aabbcc' "i3c read $i"
    Invoke-Bridge $MasterPort @('remote', 'i3c', '0x30', 'set', '0x10', '112233') | Out-Null
    Assert-Bridge (Invoke-Bridge $TargetPort @('reg', 'i3c', 'get', '0x10', '3')) '112233' "i3c write $i"
    Invoke-Bridge $TargetPort @('reg', 'i3c', 'set', '0x10', 'AABBCC') | Out-Null
}
Write-Output "i3c PASS ($Iterations rounds)"

$masterStatus = Invoke-Bridge $MasterPort @('interfaces')
$targetStatus = Invoke-Bridge $TargetPort @('interfaces')
if ($masterStatus -notmatch 'SPI ready=1 role=0 error=0x0' -or
    $masterStatus -notmatch 'I3C ready=1 role=0 error=0x0' -or
    $targetStatus -notmatch 'SPI ready=1 role=1 error=0x0' -or
    $targetStatus -notmatch 'I3C ready=1 role=1 error=0x0') {
    throw "Interface error after test:`nMaster: $masterStatus`nTarget: $targetStatus"
}
Write-Output 'all two-board buses PASS'
