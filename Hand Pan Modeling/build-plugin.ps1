param(
    [string]$BuildDirectory = "$PSScriptRoot/build-plugin-current",
    [string]$JuceDirectory,
    [string]$Generator
)
$ErrorActionPreference = 'Stop'
$cmakeCommand = Get-Command cmake.exe -ErrorAction SilentlyContinue
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = if (Test-Path -LiteralPath $vswhere) {
    & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -format json | ConvertFrom-Json
}
$cmakePath = if ($cmakeCommand) { $cmakeCommand.Source } elseif ($vs) {
    Join-Path $vs.installationPath 'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
}
if (!$cmakePath -or !(Test-Path -LiteralPath $cmakePath)) { throw 'CMake was not found.' }
$configure = @('-S', "$PSScriptRoot/plugin", '-B', $BuildDirectory)
if (!$Generator -and !(Test-Path -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt'))) {
    $Generator = switch (([version]($vs.installationVersion ?? '0.0')).Major) {
        18 { 'Visual Studio 18 2026' }
        17 { 'Visual Studio 17 2022' }
        default { throw 'Install Visual Studio with C++ tools or specify -Generator.' }
    }
}
if ($Generator) {
    $configure += @('-G', $Generator)
    if ($Generator -like 'Visual Studio*') { $configure += @('-A', 'x64') }
}
if ($JuceDirectory) { $configure += "-DJUCE_DIR=$((Resolve-Path -LiteralPath $JuceDirectory).Path)" }
& $cmakePath @configure
if ($LASTEXITCODE) { throw 'Plugin configuration failed.' }
& $cmakePath --build $BuildDirectory --config Release
if ($LASTEXITCODE) { throw 'Plugin build failed.' }
$ctestPath = Join-Path (Split-Path $cmakePath) 'ctest.exe'
& $ctestPath --test-dir $BuildDirectory -C Release --output-on-failure
if ($LASTEXITCODE) { throw 'Plugin checks failed.' }
