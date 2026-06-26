$ErrorActionPreference = 'Stop'

$toolRoot = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $toolRoot
$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'

if (-not (Test-Path -LiteralPath $csc)) {
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
}
if (-not (Test-Path -LiteralPath $csc)) {
    throw 'C# Compiler csc.exe wurde nicht gefunden.'
}

$source = Join-Path $toolRoot 'RiddleMatrixWindowsManagerLauncher.cs'
$output = Join-Path $toolRoot 'RiddleMatrixWindowsManager.exe'

$resources = @(
    @{ Path = Join-Path $toolRoot 'Start-RiddleMatrixWindowsManager.ps1'; Name = 'payload.WindowsTool.Start-RiddleMatrixWindowsManager.ps1' },
    @{ Path = Join-Path $repoRoot 'USBStick-Setup\files\usr\local\bin\webserver.py'; Name = 'payload.USBStick-Setup.files.usr.local.bin.webserver.py' },
    @{ Path = Join-Path $repoRoot 'USBStick-Setup\files\usr\local\etc\index.html'; Name = 'payload.USBStick-Setup.files.usr.local.etc.index.html' },
    @{ Path = Join-Path $repoRoot 'USBStick-Setup\files\usr\local\etc\vendor\xlsx.full.min.js'; Name = 'payload.USBStick-Setup.files.usr.local.etc.vendor.xlsx.full.min.js' }
)

foreach ($resource in $resources) {
    if (-not (Test-Path -LiteralPath $resource.Path)) {
        throw "Build-Ressource fehlt: $($resource.Path)"
    }
}

$args = @(
    '/nologo',
    '/target:winexe',
    '/optimize+',
    '/reference:System.Windows.Forms.dll',
    "/out:$output"
)

foreach ($resource in $resources) {
    $args += "/resource:$($resource.Path),$($resource.Name)"
}

$args += $source

& $csc @args
if ($LASTEXITCODE -ne 0) {
    throw "EXE-Build fehlgeschlagen mit Exitcode $LASTEXITCODE"
}

Write-Host "Erstellt: $output"
