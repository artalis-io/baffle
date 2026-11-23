param(
    [string]$HailSource = (Join-Path $PSScriptRoot '../../../hail'),
    [string]$BaffleSource = (Join-Path $PSScriptRoot '../..'),
    [string]$BuildDirectory = (Join-Path $PSScriptRoot '../../build-win32-transcript'),
    [string]$Models = ''
)
$ErrorActionPreference = 'Stop'
function Invoke-Checked {
    param([string]$Program, [string[]]$Parameters)
    & $Program @Parameters
    if ($LASTEXITCODE -ne 0) { throw "$Program failed: $LASTEXITCODE" }
}
$BaffleSource = (Resolve-Path -LiteralPath $BaffleSource).Path
$HailSource = (Resolve-Path -LiteralPath $HailSource).Path
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
$deps = Join-Path $BuildDirectory 'deps'
$sdk = Join-Path $BuildDirectory 'sdk'
$portable = Join-Path $BuildDirectory 'portable'
New-Item -ItemType Directory -Force $deps, $sdk, $portable | Out-Null
$archive = Join-Path $deps 'sherpa-static.tar.bz2'
$package = 'sherpa-onnx-v1.12.40-win-x64-static-MT-Release-no-tts-lib'
$hash = 'DC8D5EFA6EB404A8166AED06DE36A547ADE17729FE1E39FC4BF0AF08B9B91EFA'
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest -Uri "https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.40/$package.tar.bz2" -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash -ne $hash) { throw 'Static package checksum mismatch' }
Invoke-Checked -Program tar -Parameters @('-xf', $archive, '-C', $deps)
$sherpa = Join-Path $deps 'sherpa-source'
if (-not (Test-Path -LiteralPath $sherpa)) {
    Invoke-Checked -Program git -Parameters @('clone', '--depth', '1', '--branch', 'v1.12.40', 'https://github.com/k2-fsa/sherpa-onnx.git', $sherpa)
}
$revision = & git -C $sherpa rev-parse HEAD
if ($LASTEXITCODE -ne 0 -or $revision -ne 'd795ccd164bd2360ffda6c3431b1316e2c898be2') { throw 'Sherpa source revision mismatch' }
foreach ($component in @('hail','baffle')) {
    $source = if ($component -eq 'hail') { $HailSource } else { $BaffleSource }
    $build = Join-Path $BuildDirectory $component
    $options = @('-S',$source,'-B',$build,'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL',
                 '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',"-DCMAKE_INSTALL_PREFIX=$sdk")
    if ($component -eq 'hail') { $options += @('-DHA_BUILD_TESTS=OFF','-DHA_BUILD_EXAMPLES=OFF','-DHA_BUILD_SHARED=OFF') }
    else { $options += @('-DBF_BUILD_TESTS=OFF','-DBF_BUILD_BENCHMARKS=OFF','-DBF_BUILD_SHARED=OFF','-DBF_WITH_SPEEXDSP=OFF') }
    Invoke-Checked -Program cmake -Parameters $options
    Invoke-Checked -Program cmake -Parameters @('--build',$build,'--config','Release','--parallel')
    Invoke-Checked -Program cmake -Parameters @('--install',$build,'--config','Release')
}
$app = Join-Path $BuildDirectory 'app'
Invoke-Checked -Program cmake -Parameters @('-S',$PSScriptRoot,'-B',$app,'-G','Visual Studio 17 2022','-A','x64','-T','ClangCL',
    "-DCMAKE_PREFIX_PATH=$sdk","-DSHERPA_SOURCE_DIR=$sherpa","-DSHERPA_STATIC_DIR=$(Join-Path $deps $package)")
Invoke-Checked -Program cmake -Parameters @('--build',$app,'--config','Release','--parallel')
Copy-Item -LiteralPath (Join-Path $app 'Release/BaffleLive.exe') -Destination $portable
if ($Models) {
    $modelDir = Join-Path $portable 'models/parakeet'
    New-Item -ItemType Directory -Force $modelDir | Out-Null
    foreach ($file in @('encoder.int8.onnx','decoder.int8.onnx','joiner.int8.onnx','tokens.txt')) {
        Copy-Item -LiteralPath (Join-Path $Models $file) -Destination $modelDir
    }
    if (Test-Path -LiteralPath (Join-Path $Models 'README.md')) { Copy-Item -LiteralPath (Join-Path $Models 'README.md') -Destination $modelDir }
}
$notices = Join-Path $portable 'licenses'
New-Item -ItemType Directory -Force $notices | Out-Null
Copy-Item -LiteralPath (Join-Path $BaffleSource 'LICENSE') -Destination (Join-Path $notices 'Baffle.txt')
Copy-Item -LiteralPath (Join-Path $HailSource 'LICENSE') -Destination (Join-Path $notices 'Hail.txt')
Copy-Item -LiteralPath (Join-Path $sherpa 'LICENSE') -Destination (Join-Path $notices 'sherpa-onnx.txt')
Write-Output "Native executable: $(Join-Path $portable 'BaffleLive.exe')"
