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
$tempResourceDir = $null
$startScriptPath = Join-Path $toolRoot 'Start-RiddleMatrixWindowsManager.ps1'

if ($env:RIDDLEMATRIX_MANAGER_DEFAULT_SSID -or $env:RIDDLEMATRIX_MANAGER_DEFAULT_PASSWORD) {
    $tempResourceDir = Join-Path ([IO.Path]::GetTempPath()) ('RiddleMatrixManagerBuild-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tempResourceDir -Force | Out-Null
    $patchedStartScript = Join-Path $tempResourceDir 'Start-RiddleMatrixWindowsManager.ps1'
    $scriptContent = Get-Content -LiteralPath $startScriptPath -Raw
    if ($env:RIDDLEMATRIX_MANAGER_DEFAULT_SSID) {
        $escapedSsid = $env:RIDDLEMATRIX_MANAGER_DEFAULT_SSID.Replace("'", "''")
        $scriptContent = [regex]::Replace($scriptContent, "Ssid = '[^']*'", "Ssid = '$escapedSsid'", 1)
    }
    if ($env:RIDDLEMATRIX_MANAGER_DEFAULT_PASSWORD) {
        $escapedPassword = $env:RIDDLEMATRIX_MANAGER_DEFAULT_PASSWORD.Replace("'", "''")
        $scriptContent = [regex]::Replace($scriptContent, "Password = '[^']*'", "Password = '$escapedPassword'", 1)
    }
    Set-Content -LiteralPath $patchedStartScript -Value $scriptContent -NoNewline -Encoding UTF8
    $startScriptPath = $patchedStartScript
}

$resources = @(
    @{ Path = $startScriptPath; Name = 'payload.WindowsTool.Start-RiddleMatrixWindowsManager.ps1' },
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

if ($tempResourceDir) {
    Remove-Item -LiteralPath $tempResourceDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host "Erstellt: $output"
