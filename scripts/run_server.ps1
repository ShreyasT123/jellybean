param(
    [string]$BuildDir = "build",
    [string]$Config = "Release",
    [string]$ServerConfig = "server.config",
    [string]$LibTorchRoot = "C:/deps/libtorch"
)

$TorchConfig = Join-Path $LibTorchRoot "share/cmake/Torch/TorchConfig.cmake"
Write-Host "Using LIBTORCH_ROOT=$LibTorchRoot"
if (-not (Test-Path $TorchConfig)) {
    Write-Error "TorchConfig.cmake not found at: $TorchConfig"
    exit 1
}

cmake -S . -B "$BuildDir" "-DENABLE_TORCH=ON" "-DLIBTORCH_ROOT=$($LibTorchRoot)"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build "$BuildDir" --config "$Config" --target jellybean_infer_server_demo
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& ".\$BuildDir\src\$Config\jellybean_infer_server_demo.exe" $ServerConfig
