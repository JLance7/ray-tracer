$ErrorActionPreference = 'Stop'
Set-Location -LiteralPath $PSScriptRoot
try {
    $compiler = Get-Command gcc.exe -ErrorAction SilentlyContinue
    if ($compiler) { $compilerPath = $compiler.Source }
    elseif (Test-Path -LiteralPath 'C:/msys64/mingw64/bin/gcc.exe') { $compilerPath = 'C:/msys64/mingw64/bin/gcc.exe' }
    elseif (Test-Path -LiteralPath 'C:/msys64/ucrt64/bin/gcc.exe') { $compilerPath = 'C:/msys64/ucrt64/bin/gcc.exe' }
    else { throw 'Install MSYS2 MinGW-w64 GCC to compile. The included build/IronVeil.exe can already be played without a compiler.' }
    New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
    & $compilerPath '-std=c11' '-O2' '-Wall' '-Wextra' '-Wpedantic' '-municode' '-mwindows' '-static' '-static-libgcc' (Join-Path $PSScriptRoot 'src/main.c') '-o' (Join-Path $PSScriptRoot 'build/IronVeil.exe') '-luser32' '-lgdi32' '-lm'
    if ($LASTEXITCODE -ne 0) { throw 'C compilation failed.' }
    Write-Host 'Built build/IronVeil.exe' -ForegroundColor Green
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
