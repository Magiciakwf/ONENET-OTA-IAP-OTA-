param([string]$Compiler = 'C:/msys64/ucrt64/bin/gcc.exe')

$ErrorActionPreference = 'Stop'
$repoPath = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$testOutput = Join-Path $env:TEMP ('stm32-host-tests-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testOutput | Out-Null

Push-Location $repoPath
try {
    foreach ($testName in @('test_health_led', 'test_can')) {
        $testExe = Join-Path $testOutput ($testName + '.exe')
        & $Compiler '-std=c99' '-Wall' '-Wextra' '-Werror' '-Itests/health_led/stubs' '-I18FreeRTOS项目/User' "tests/health_led/$testName.c" '-o' $testExe
        if ($LASTEXITCODE -ne 0) { throw "Compile failed: $testName" }
        & $testExe
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $testName" }
    }
    Write-Output "Test outputs: $testOutput"
}
finally { Pop-Location }
