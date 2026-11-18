param(
    [string]$BuildDirectory = "$PSScriptRoot/build-plugin-current",
    [string]$InnoCompiler,
    [ValidatePattern('^[a-fA-F0-9]{40}$')][string]$CertificateThumbprint,
    [string]$TimestampUrl,
    [string]$SignTool = 'signtool.exe'
)
$ErrorActionPreference = 'Stop'
if (!$InnoCompiler) {
    $InnoCompiler = (Get-Command ISCC.exe -ErrorAction SilentlyContinue).Source
    if (!$InnoCompiler) {
        foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles, "$env:LOCALAPPDATA/Programs")) {
            if (!$root) { continue }
            $candidate = Join-Path $root 'Inno Setup 6/ISCC.exe'
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                $InnoCompiler = $candidate
                break
            }
        }
    }
}
if (!$InnoCompiler -or !(Test-Path -LiteralPath $InnoCompiler -PathType Leaf)) {
    throw 'Inno Setup 6.4+ compiler not found. Install Inno Setup or specify -InnoCompiler.'
}

$version = (Get-Content -LiteralPath "$PSScriptRoot/VERSION" -Raw).Trim()
$packageArguments = @{ BuildDirectory = $BuildDirectory }
if ($CertificateThumbprint) {
    $packageArguments += @{
        CertificateThumbprint = $CertificateThumbprint
        TimestampUrl = $TimestampUrl
        SignTool = $SignTool
    }
}
$releaseZip = & "$PSScriptRoot/package-plugin.ps1" @packageArguments
$dist = Split-Path -Parent $releaseZip
$name = "SONFORM Void $version Windows Setup.exe"
$stageParent = (Resolve-Path -LiteralPath $BuildDirectory).Path
$stage = Join-Path $stageParent ('installer-stage-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null

try {
    $release = [IO.Compression.ZipFile]::OpenRead($releaseZip)
    try {
        $sourceZip = [IO.Compression.ZipFile]::Open((Join-Path $stage 'Source.zip'), 'Create')
        try {
            foreach ($entry in $release.Entries) {
                if ($entry.FullName.StartsWith('Source/', [StringComparison]::Ordinal)) {
                    $destination = $sourceZip.CreateEntry($entry.FullName.Substring(7))
                    $inputStream = $entry.Open()
                    try {
                        $outputStream = $destination.Open()
                        try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
                    } finally { $inputStream.Dispose() }
                }
            }
        } finally { $sourceZip.Dispose() }

        $bundle = 'SONFORM Void.vst3'
        foreach ($entryName in @("$bundle/Contents/x86_64-win/$bundle", "$bundle/Contents/Resources/moduleinfo.json",
                "SONFORM Void $version.exe", 'LICENSE.md', 'README.md')) {
            $destination = Join-Path $stage $entryName
            New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($release.GetEntry($entryName), $destination)
        }
    } finally { $release.Dispose() }

    $script = Join-Path $PSScriptRoot 'installer/SONFORM-Void.iss'
    & $InnoCompiler "/DReleaseVersion=$version" "/DPayload=$stage" "/DSetupOutput=$stage" $script | Out-Host
    if ($LASTEXITCODE) { throw 'Installer compilation failed.' }
    $setup = Join-Path $stage $name
    if ($CertificateThumbprint) {
        & $SignTool sign /sha1 $CertificateThumbprint /fd SHA256 /tr $TimestampUrl /td SHA256 $setup | Out-Host
        if ($LASTEXITCODE) { throw 'Installer signing failed.' }
        & $SignTool verify /pa $setup | Out-Host
        if ($LASTEXITCODE) { throw 'Installer signature verification failed.' }
    }
    $destination = Join-Path $dist $name
    Move-Item -LiteralPath $setup -Destination $destination -Force
    Write-Output $destination
} finally {
    $resolvedStage = (Resolve-Path -LiteralPath $stage).Path
    if ((Split-Path -Parent $resolvedStage) -eq $stageParent -and (Split-Path -Leaf $resolvedStage) -like 'installer-stage-*') {
        Remove-Item -LiteralPath $resolvedStage -Recurse -Force
    }
}
