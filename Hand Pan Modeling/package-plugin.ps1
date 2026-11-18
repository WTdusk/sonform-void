param(
    [string]$BuildDirectory = "$PSScriptRoot/build-plugin-current",
    [ValidatePattern('^[a-fA-F0-9]{40}$')][string]$CertificateThumbprint,
    [string]$TimestampUrl,
    [string]$SignTool = 'signtool.exe'
)
$ErrorActionPreference = 'Stop'
$version = (Get-Content -LiteralPath "$PSScriptRoot/VERSION" -Raw).Trim()
$release = Join-Path $BuildDirectory 'HandpanStudio_artefacts/Release'
$bundle = 'SONFORM Void.vst3'
$standalone = "SONFORM Void $version.exe"
$module = "$bundle/Contents/x86_64-win/$bundle"
$metadata = "$bundle/Contents/Resources/moduleinfo.json"
$manifest = Get-Content -LiteralPath "$release/VST3/$metadata" -Raw
if ($manifest -notmatch ('"Version"\s*:\s*"' + [regex]::Escape($version) + '"') -or
    $manifest -notmatch '"Vendor"\s*:\s*"WTdusk"') {
    throw 'The VST3 manifest does not match VERSION and the release vendor. Build first.'
}
$files = @(
    @{ Source = Join-Path $release "VST3/$module"; Name = $module; Binary = $true }
    @{ Source = Join-Path $release "VST3/$metadata"; Name = $metadata }
    @{ Source = Join-Path $release "Standalone/$standalone"; Name = $standalone; Binary = $true }
    @{ Source = Join-Path $PSScriptRoot 'plugin/README.md'; Name = 'README.md' }
    @{ Source = Join-Path $PSScriptRoot 'LICENSE.md'; Name = 'LICENSE.md' }
)
foreach ($file in $files | Where-Object { $_.Binary }) {
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $file.Source).Path)
    if ($info.CompanyName -ne 'WTdusk' -or $info.FileVersion -ne $version) {
        throw "Windows file metadata does not match VERSION and the release vendor in $($file.Name). Build first."
    }
}
$sourceRoot = 'Source/Hand Pan Modeling'
foreach ($name in '.gitignore','VERSION','CMakeLists.txt','README.md','LICENSE.md','build-plugin.ps1','package-plugin.ps1','build-installer.ps1','Start-HandpanStudio.cmd') {
    $files += @{ Source = Join-Path $PSScriptRoot $name; Name = "$sourceRoot/$name" }
}
$juceRoot = (Resolve-Path -LiteralPath ((Get-Content -LiteralPath "$BuildDirectory/sonform-juce-source.txt" -Raw).Trim())).Path
$sourceTrees = @(
    @{ Path = Join-Path $PSScriptRoot 'src'; Name = "$sourceRoot/src" }
    @{ Path = Join-Path $PSScriptRoot 'plugin'; Name = "$sourceRoot/plugin" }
    @{ Path = Join-Path $PSScriptRoot 'installer'; Name = "$sourceRoot/installer" }
    @{ Path = Join-Path $juceRoot 'modules'; Name = 'Source/JUCE-9.0.2/modules' }
    @{ Path = Join-Path $juceRoot 'extras/Build'; Name = 'Source/JUCE-9.0.2/extras/Build' }
)
# Keep build inputs and their notices, excluding upstream examples and optional tools.
foreach ($name in 'CMakeLists.txt','JUCE.spdx.json','LICENSE.md','README.md','CHANGE_LIST.md','BREAKING_CHANGES.md','CODE_OF_CONDUCT.md','SECURITY.md') {
    $files += @{ Source = Join-Path $juceRoot $name; Name = "Source/JUCE-9.0.2/$name" }
}
foreach ($tree in $sourceTrees) {
    foreach ($file in Get-ChildItem -LiteralPath $tree.Path -File -Recurse) {
        $relative = [IO.Path]::GetRelativePath($tree.Path, $file.FullName).Replace('\','/')
        $files += @{ Source = $file.FullName; Name = "$($tree.Name)/$relative" }
    }
}

$forbidden = '(?i)hang[ _.-]*(?:mk[ .]*[12]|[12](?:\b|[_-]))|halo[ _]+(?:in[ _]+)?[bc]\b|meridian|rav[ _-]+vast|liminal|rock[ _-]+creek|soniccouture|kontakt|pan[ _-]+drums|warm_original|\.nc[kw]\b'
foreach ($file in $files | Where-Object { $_.Binary }) {
    $bytes = [IO.File]::ReadAllBytes($file.Source)
    if ([Text.Encoding]::Latin1.GetString($bytes) -match $forbidden -or
        [Text.Encoding]::Unicode.GetString($bytes) -match $forbidden -or
        [Text.Encoding]::Unicode.GetString($bytes, 1, $bytes.Length - 1) -match $forbidden) {
        throw "Unexpected source branding in $($file.Name)."
    }
}

Add-Type -AssemblyName System.IO.Compression.FileSystem
$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Force -Path $dist | Out-Null
$path = Join-Path $dist "SONFORM Void $version Windows.zip"
$temp = "$path.$([guid]::NewGuid()).tmp"
$signedCopies = @()
try {
    if ($CertificateThumbprint) {
        if (!$TimestampUrl) { throw '-TimestampUrl is required with -CertificateThumbprint.' }
        foreach ($file in $files | Where-Object { $_.Binary }) {
            $copy = "$temp.$([guid]::NewGuid())$([IO.Path]::GetExtension($file.Source))"
            $signedCopies += $copy
            Copy-Item -LiteralPath $file.Source -Destination $copy
            & $SignTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $copy | Out-Host
            if ($LASTEXITCODE) { throw "Signing failed for $($file.Name)." }
            & $SignTool verify /pa $copy | Out-Host
            if ($LASTEXITCODE) { throw "Signature verification failed for $($file.Name)." }
            $file.Source = $copy
        }
    } else {
        Write-Warning 'No Authenticode certificate specified. Packaging unsigned binaries.'
    }
    $archive = [IO.Compression.ZipFile]::Open($temp, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in $files) {
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.Source, $file.Name) | Out-Null
        }
    } finally { $archive.Dispose() }
    Move-Item -LiteralPath $temp -Destination $path -Force
} finally {
    foreach ($file in @($temp) + $signedCopies) {
        if (Test-Path -LiteralPath $file) { Remove-Item -LiteralPath $file }
    }
}
Write-Output $path
