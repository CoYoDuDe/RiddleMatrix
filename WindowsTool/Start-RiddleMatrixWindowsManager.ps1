Add-Type -AssemblyName PresentationFramework
Add-Type -AssemblyName System.Windows.Forms

$toolRoot = Split-Path -Parent $PSCommandPath
$repoRoot = Split-Path -Parent $toolRoot
$serverScript = Join-Path $repoRoot 'USBStick-Setup\files\usr\local\bin\webserver.py'
$indexFile = Join-Path $repoRoot 'USBStick-Setup\files\usr\local\etc\index.html'
$vendorDir = Join-Path $repoRoot 'USBStick-Setup\files\usr\local\etc\vendor'
$settingsDir = Join-Path $env:APPDATA 'RiddleMatrixWindowsManager'
$settingsPath = Join-Path $settingsDir 'settings.json'
$configPath = Join-Path $settingsDir 'boxen_config.json'
$venvDir = Join-Path $settingsDir 'venv'
$venvPython = Join-Path $venvDir 'Scripts\python.exe'

function Ensure-SettingsDirectory {
    if (-not (Test-Path -LiteralPath $settingsDir)) {
        New-Item -ItemType Directory -Path $settingsDir -Force | Out-Null
    }
}

function Get-DefaultSettings {
    [ordered]@{
        Ssid = 'RiddleMatrix_AP'
        Password = 'RiddleMatrix-Setup!'
        ManagerPort = 8080
        BoxSubnet = '192.168.137'
        ServerPid = 0
    }
}

function Load-Settings {
    $defaults = Get-DefaultSettings
    if (-not (Test-Path -LiteralPath $settingsPath)) {
        return [pscustomobject]$defaults
    }

    try {
        $loaded = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        foreach ($key in $defaults.Keys) {
            if ($null -eq $loaded.$key) {
                $loaded | Add-Member -NotePropertyName $key -NotePropertyValue $defaults[$key] -Force
            }
        }
        return $loaded
    }
    catch {
        [System.Windows.MessageBox]::Show(
            "Die gespeicherten Einstellungen konnten nicht gelesen werden. Standardwerte werden verwendet.`n`n$($_.Exception.Message)",
            'RiddleMatrix Windows Manager',
            'OK',
            'Warning'
        ) | Out-Null
        return [pscustomobject]$defaults
    }
}

function Save-Settings {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Settings
    )

    Ensure-SettingsDirectory
    $Settings | ConvertTo-Json | Set-Content -LiteralPath $settingsPath -Encoding UTF8
}

function ConvertTo-ShellSingleQuotedValue {
    param([string]$Value)
    $quote = [string][char]39
    $replacement = $quote + '"' + $quote + '"' + $quote
    $quote + $Value.Replace($quote, $replacement) + $quote
}

function Get-RiddleMatrixUsbDrives {
    $drives = @()

    try {
        $removable = Get-CimInstance Win32_LogicalDisk -Filter "DriveType=2" -ErrorAction Stop
        foreach ($drive in $removable) {
            if (-not [string]::IsNullOrWhiteSpace($drive.DeviceID)) {
                $drives += (($drive.DeviceID.TrimEnd('\') + '\'))
            }
        }
    }
    catch {
    }

    $currentRoot = [System.IO.Path]::GetPathRoot($toolRoot)
    if (-not [string]::IsNullOrWhiteSpace($currentRoot)) {
        $drives += $currentRoot
    }

    $drives |
        Sort-Object -Unique |
        Where-Object {
            (Test-Path -LiteralPath (Join-Path $_ 'RIDDLEMATRIX_USB.txt')) -or
            (Test-Path -LiteralPath (Join-Path $_ 'config\public_ap.env')) -or
            (Test-Path -LiteralPath (Join-Path $_ 'WindowsTool\Start-RiddleMatrixWindowsManager.ps1'))
        }
}

function Write-HotspotConfigToUsb {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Settings
    )

    $targets = @(Get-RiddleMatrixUsbDrives)
    if ($targets.Count -eq 0) {
        throw 'Kein RiddleMatrix-USB-Stick gefunden. Bitte den Stick unter Windows einstecken und erneut versuchen.'
    }

    $written = @()
    $encoding = New-Object System.Text.UTF8Encoding($false)
    foreach ($driveRoot in $targets) {
        $configDir = Join-Path $driveRoot 'config'
        $configFile = Join-Path $configDir 'public_ap.env'
        New-Item -ItemType Directory -Path $configDir -Force | Out-Null

        $content = @(
            '# RiddleMatrix Hotspot-Konfiguration fuer den Linux-Boot vom USB-Stick'
            '# Diese Datei wird vom Windows Manager geschrieben.'
            ('SSID=' + (ConvertTo-ShellSingleQuotedValue -Value $Settings.Ssid))
            ('WPA_PASSPHRASE=' + (ConvertTo-ShellSingleQuotedValue -Value $Settings.Password))
            ''
        ) -join "`n"
        [System.IO.File]::WriteAllText($configFile, $content, $encoding)
        $written += $configFile
    }

    $written
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Restart-AsAdministrator {
    Start-Process -FilePath 'powershell.exe' -ArgumentList @(
        '-ExecutionPolicy', 'Bypass',
        '-File', "`"$PSCommandPath`""
    ) -Verb RunAs | Out-Null
}

function Get-SystemPython {
    $candidates = @(
        @{ File = 'py.exe'; Args = @('-3') },
        @{ File = 'python.exe'; Args = @() }
    )

    foreach ($candidate in $candidates) {
        try {
            $checkArgs = @($candidate.Args) + @('-c', 'import sys; print(sys.executable)')
            $output = & $candidate.File @checkArgs 2>$null
            if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($output)) {
                return [pscustomobject]@{
                    File = $candidate.File
                    Args = @($candidate.Args)
                }
            }
        }
        catch {
        }
    }

    throw 'Python 3 wurde nicht gefunden. Bitte Python 3 installieren oder den Python Launcher py.exe verfuegbar machen.'
}

function Test-PythonPackages {
    param([string]$PythonExe)

    & $PythonExe -c 'import flask, bs4, requests' 2>$null
    return ($LASTEXITCODE -eq 0)
}

function Ensure-PythonRuntime {
    Ensure-SettingsDirectory

    if ((Test-Path -LiteralPath $venvPython) -and (Test-PythonPackages -PythonExe $venvPython)) {
        return $venvPython
    }

    $systemPython = Get-SystemPython
    if (-not (Test-Path -LiteralPath $venvPython)) {
        & $systemPython.File @($systemPython.Args + @('-m', 'venv', $venvDir))
        if ($LASTEXITCODE -ne 0) {
            throw 'Python-Umgebung konnte nicht erstellt werden.'
        }
    }

    & $venvPython -m pip install --upgrade pip
    if ($LASTEXITCODE -ne 0) {
        throw 'pip konnte in der lokalen Python-Umgebung nicht aktualisiert werden.'
    }

    & $venvPython -m pip install 'Flask>=2.3,<3.0' 'beautifulsoup4>=4.12,<5' 'requests>=2.31,<3'
    if ($LASTEXITCODE -ne 0 -or -not (Test-PythonPackages -PythonExe $venvPython)) {
        throw 'Python-Abhaengigkeiten fuer den originalen RiddleMatrix-Webserver konnten nicht installiert werden.'
    }

    $venvPython
}

function Invoke-Netsh {
    param(
        [Parameter(Mandatory = $true)]
        [string[]]$Arguments
    )

    $output = & netsh @Arguments 2>&1 | Out-String
    [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $output.Trim()
    }
}

function Wait-WinRtAsyncOperation {
    param(
        [Parameter(Mandatory = $true)]
        [object]$Operation,
        [Parameter(Mandatory = $true)]
        [type]$ResultType
    )

    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and
            $_.IsGenericMethodDefinition -and
            $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        } |
        Select-Object -First 1)

    $task = $asTaskGeneric.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $task.Wait()
    $task.Result
}

function Get-ConnectedWifiName {
    $result = Invoke-Netsh -Arguments @('wlan', 'show', 'interfaces')
    if ($result.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($result.Output)) {
        return $null
    }

    $ssid = $null
    $isConnected = $false
    foreach ($line in ($result.Output -split "`r?`n")) {
        if ($line -match '^\s*(State|Status)\s*:\s*(.+)$') {
            $state = $Matches[2].Trim().ToLowerInvariant()
            if ($state -in @('connected', 'verbunden')) {
                $isConnected = $true
            }
        }

        if ($line -match '^\s*SSID\s*:\s*(.+)$' -and $line -notmatch '^\s*BSSID') {
            $ssid = $Matches[1].Trim()
        }
    }

    if ($isConnected -and -not [string]::IsNullOrWhiteSpace($ssid)) {
        return $ssid
    }

    $null
}

function Test-HostedNetworkSupport {
    $result = Invoke-Netsh -Arguments @('wlan', 'show', 'drivers')
    if ($result.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($result.Output)) {
        return $true
    }

    foreach ($line in ($result.Output -split "`r?`n")) {
        if ($line -match '(?i)(hosted network|gehostete).*:\s*(.+)$') {
            $value = $Matches[2].Trim().ToLowerInvariant()
            if ($value -match '^(no|nein)') {
                return $false
            }
            if ($value -match '^(yes|ja)') {
                return $true
            }
        }
    }

    return $true
}

function Confirm-WifiDisconnectForAp {
    $connectedWifi = Get-ConnectedWifiName
    if (-not $connectedWifi) {
        return
    }

    $answer = [System.Windows.MessageBox]::Show(
        "Das Notebook ist aktuell mit dem WLAN '$connectedWifi' verbunden.`n`nJa = WLAN trennen und AP starten.`nNein = verbunden bleiben und Windows Mobile Hotspot versuchen.`nAbbrechen = nichts starten.",
        'WLAN-Verbindung gefunden',
        'YesNoCancel',
        'Question'
    )

    if ($answer -eq [System.Windows.MessageBoxResult]::Cancel) {
        throw 'AP-Start abgebrochen.'
    }
    if ($answer -ne [System.Windows.MessageBoxResult]::Yes) {
        return
    }

    $disconnect = Invoke-Netsh -Arguments @('wlan', 'disconnect')
    if ($disconnect.ExitCode -ne 0) {
        throw "WLAN konnte nicht getrennt werden.`n$($disconnect.Output)"
    }
}

function Start-MobileHotspot {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Ssid,
        [Parameter(Mandatory = $true)]
        [string]$Password
    )

    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    [Windows.Networking.Connectivity.NetworkInformation, Windows.Networking.Connectivity, ContentType = WindowsRuntime] | Out-Null
    [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime] | Out-Null
    [Windows.Networking.NetworkOperators.NetworkOperatorTetheringSessionAccessPointConfiguration, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime] | Out-Null
    [Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult, Windows.Networking.NetworkOperators, ContentType = WindowsRuntime] | Out-Null

    $profile = [Windows.Networking.Connectivity.NetworkInformation]::GetInternetConnectionProfile()
    if ($null -eq $profile) {
        throw 'Windows Mobile Hotspot braucht ein aktives Netzwerkprofil. Oeffne die Windows-Hotspot-Einstellungen und starte den Hotspot dort einmal manuell.'
    }

    $manager = [Windows.Networking.NetworkOperators.NetworkOperatorTetheringManager]::CreateFromConnectionProfile($profile)
    $config = New-Object Windows.Networking.NetworkOperators.NetworkOperatorTetheringSessionAccessPointConfiguration
    $config.Ssid = $Ssid
    $config.Passphrase = $Password

    $operation = $manager.StartTetheringAsync($config)
    $result = Wait-WinRtAsyncOperation -Operation $operation -ResultType ([Windows.Networking.NetworkOperators.NetworkOperatorTetheringOperationResult])

    if ([string]$result.Status -ne 'Success') {
        $message = if ([string]::IsNullOrWhiteSpace($result.AdditionalErrorMessage)) {
            "Status: $($result.Status)"
        }
        else {
            "Status: $($result.Status)`n$($result.AdditionalErrorMessage)"
        }
        throw "Windows Mobile Hotspot konnte nicht gestartet werden.`n$message"
    }

    'Windows Mobile Hotspot wurde gestartet.'
}

function Validate-Settings {
    param(
        [string]$Ssid,
        [string]$Password,
        [int]$ManagerPort,
        [string]$BoxSubnet
    )

    if ([string]::IsNullOrWhiteSpace($Ssid)) {
        throw 'SSID darf nicht leer sein.'
    }
    if ($Ssid.Length -gt 32) {
        throw 'SSID darf maximal 32 Zeichen lang sein.'
    }
    if ([string]::IsNullOrWhiteSpace($Password) -or $Password.Length -lt 8 -or $Password.Length -gt 63) {
        throw 'WLAN-Passwort muss zwischen 8 und 63 Zeichen lang sein.'
    }
    if ($ManagerPort -lt 1 -or $ManagerPort -gt 65535) {
        throw 'Manager-Port muss zwischen 1 und 65535 liegen.'
    }
    if ($BoxSubnet -notmatch '^\d{1,3}\.\d{1,3}\.\d{1,3}$') {
        throw 'Box-Subnetz muss wie 192.168.137 angegeben werden.'
    }
}

function Start-WindowsAccessPoint {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Ssid,
        [Parameter(Mandatory = $true)]
        [string]$Password
    )

    Confirm-WifiDisconnectForAp

    try {
        return Start-MobileHotspot -Ssid $Ssid -Password $Password
    }
    catch {
        if (-not (Test-HostedNetworkSupport)) {
            throw
        }
    }

    $configure = Invoke-Netsh -Arguments @('wlan', 'set', 'hostednetwork', 'mode=allow', "ssid=$Ssid", "key=$Password")
    if ($configure.ExitCode -ne 0) {
        throw "AP-Konfiguration fehlgeschlagen.`n$($configure.Output)"
    }

    $start = Invoke-Netsh -Arguments @('wlan', 'start', 'hostednetwork')
    if ($start.ExitCode -ne 0 -or $start.Output -match 'couldn.t|nicht gestartet|failed|Fehler') {
        throw "AP konnte nicht gestartet werden.`n$($start.Output)"
    }

    $start.Output
}

function Stop-WindowsAccessPoint {
    if (Test-HostedNetworkSupport) {
        $stop = Invoke-Netsh -Arguments @('wlan', 'stop', 'hostednetwork')
        if ($stop.ExitCode -eq 0 -and $stop.Output -notmatch 'failed|Fehler') {
            return $stop.Output
        }
    }

    Start-Process 'ms-settings:network-mobilehotspot' | Out-Null
    'Windows Mobile Hotspot bitte in den geoeffneten Windows-Einstellungen ausschalten.'
}

function Get-ProcessBySavedPid {
    param([int]$SavedProcessId)
    if ($SavedProcessId -le 0) {
        return $null
    }

    try {
        Get-Process -Id $SavedProcessId -ErrorAction Stop
    }
    catch {
        $null
    }
}

function Start-ManagerServer {
    param(
        [Parameter(Mandatory = $true)]
        [pscustomobject]$Settings
    )

    if (-not (Test-Path -LiteralPath $serverScript)) {
        throw "Server-Skript fehlt: $serverScript"
    }
    if (-not (Test-Path -LiteralPath $indexFile)) {
        throw "Original-Weboberflaeche fehlt: $indexFile"
    }

    $existing = Get-ProcessBySavedPid -SavedProcessId ([int]$Settings.ServerPid)
    if ($existing) {
        return [int]$existing.Id
    }

    $pythonExe = Ensure-PythonRuntime
    $env:RIDDLEMATRIX_CONFIG_FILE = $configPath
    $env:RIDDLEMATRIX_INDEX_FILE = $indexFile
    $env:RIDDLEMATRIX_VENDOR_DIR = $vendorDir
    $env:RIDDLEMATRIX_SCAN_SUBNET = $Settings.BoxSubnet
    $env:RIDDLEMATRIX_ENABLE_ARP_SCAN = '1'
    $env:RIDDLEMATRIX_HIDE_SHUTDOWN = '1'
    $env:RIDDLEMATRIX_BOX_MANAGER_KEY = [string]$Settings.Password
    $env:RIDDLEMATRIX_SERVER_HOST = '127.0.0.1'
    $env:RIDDLEMATRIX_SERVER_PORT = [string]$Settings.ManagerPort
    $env:SHUTDOWN_COMMAND = ''

    $process = Start-Process -FilePath $pythonExe -ArgumentList @(
        "`"$serverScript`"",
        '--host', '127.0.0.1',
        '--port', ([string]$Settings.ManagerPort)
    ) -WindowStyle Hidden -PassThru

    Start-Sleep -Milliseconds 1200
    [int]$process.Id
}

function Stop-ManagerServer {
    param([int]$SavedProcessId)

    $process = Get-ProcessBySavedPid -SavedProcessId $SavedProcessId
    if ($process) {
        Stop-Process -Id $process.Id -Force
    }
}

[xml]$xaml = @'
<Window xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
        xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
        Title="RiddleMatrix Windows Manager"
        Height="560"
        Width="820"
        MinWidth="760"
        MinHeight="560"
        WindowStartupLocation="CenterScreen"
        ResizeMode="CanResize"
        Background="#F3F4F6">
    <Grid Margin="18">
        <Grid.RowDefinitions>
            <RowDefinition Height="Auto"/>
            <RowDefinition Height="*"/>
            <RowDefinition Height="Auto"/>
        </Grid.RowDefinitions>

        <Border Background="#111827" CornerRadius="10" Padding="18" Margin="0,0,0,16">
            <StackPanel>
                <TextBlock Text="RiddleMatrix Windows Manager" Foreground="White" FontSize="24" FontWeight="SemiBold"/>
                <TextBlock Text="Startet den Windows-Hotspot und die lokale Boxenverwaltung ohne USB-Boot." Foreground="#D1D5DB" TextWrapping="Wrap" Margin="0,8,0,0"/>
            </StackPanel>
        </Border>

        <Border Grid.Row="1" Background="White" CornerRadius="10" Padding="18">
            <Grid>
                <Grid.RowDefinitions>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="Auto"/>
                    <RowDefinition Height="*"/>
                </Grid.RowDefinitions>
                <Grid.ColumnDefinitions>
                    <ColumnDefinition Width="165"/>
                    <ColumnDefinition Width="*"/>
                    <ColumnDefinition Width="120"/>
                </Grid.ColumnDefinitions>

                <TextBlock Grid.Row="0" Grid.Column="0" Text="AP-SSID" VerticalAlignment="Center" FontWeight="SemiBold"/>
                <TextBox Grid.Row="0" Grid.Column="1" Grid.ColumnSpan="2" Name="SsidBox" Margin="0,0,0,12" Padding="10,8"/>

                <TextBlock Grid.Row="1" Grid.Column="0" Text="AP-Passwort" VerticalAlignment="Center" FontWeight="SemiBold"/>
                <PasswordBox Grid.Row="1" Grid.Column="1" Name="PasswordBox" Margin="0,0,10,12" Padding="10,8"/>
                <Button Grid.Row="1" Grid.Column="2" Name="ShowPasswordButton" Margin="0,0,0,12" Padding="10,8" Content="Anzeigen"/>

                <TextBlock Grid.Row="2" Grid.Column="0" Text="Manager-Port" VerticalAlignment="Center" FontWeight="SemiBold"/>
                <TextBox Grid.Row="2" Grid.Column="1" Grid.ColumnSpan="2" Name="PortBox" Margin="0,0,0,12" Padding="10,8"/>

                <TextBlock Grid.Row="3" Grid.Column="0" Text="Box-Subnetz" VerticalAlignment="Center" FontWeight="SemiBold"/>
                <TextBox Grid.Row="3" Grid.Column="1" Grid.ColumnSpan="2" Name="SubnetBox" Margin="0,0,0,14" Padding="10,8"/>

                <StackPanel Grid.Row="4" Grid.Column="1" Grid.ColumnSpan="2" Orientation="Horizontal" Margin="0,0,0,14">
                    <Button Name="StartAllButton" Content="AP + Manager starten" Padding="14,10" Background="#047857" Foreground="White" Margin="0,0,10,0"/>
                    <Button Name="StartManagerButton" Content="Nur Manager starten" Padding="14,10" Margin="0,0,10,0"/>
                    <Button Name="StopAllButton" Content="Stoppen" Padding="14,10"/>
                </StackPanel>

                <Border Grid.Row="6" Grid.Column="0" Grid.ColumnSpan="3" Background="#F9FAFB" CornerRadius="8" Padding="12">
                    <TextBlock Name="StatusBlock" TextWrapping="Wrap" Foreground="#1F2937"/>
                </Border>
            </Grid>
        </Border>

        <StackPanel Grid.Row="2" Orientation="Horizontal" HorizontalAlignment="Right" Margin="0,16,0,0">
            <Button Name="SaveButton" Content="Einstellungen speichern" Padding="14,10" Margin="0,0,12,0"/>
            <Button Name="SyncUsbButton" Content="USB-Stick WLAN speichern" Padding="14,10" Margin="0,0,12,0"/>
            <Button Name="OpenHotspotSettingsButton" Content="Windows-Hotspot Einstellungen" Padding="14,10"/>
        </StackPanel>
    </Grid>
</Window>
'@

$reader = New-Object System.Xml.XmlNodeReader $xaml
$window = [Windows.Markup.XamlReader]::Load($reader)

$ssidBox = $window.FindName('SsidBox')
$passwordBox = $window.FindName('PasswordBox')
$portBox = $window.FindName('PortBox')
$subnetBox = $window.FindName('SubnetBox')
$statusBlock = $window.FindName('StatusBlock')
$showPasswordButton = $window.FindName('ShowPasswordButton')
$startAllButton = $window.FindName('StartAllButton')
$startManagerButton = $window.FindName('StartManagerButton')
$stopAllButton = $window.FindName('StopAllButton')
$saveButton = $window.FindName('SaveButton')
$syncUsbButton = $window.FindName('SyncUsbButton')
$openHotspotSettingsButton = $window.FindName('OpenHotspotSettingsButton')

$savedSettings = Load-Settings

$ssidBox.Text = [string]$savedSettings.Ssid
$passwordBox.Password = [string]$savedSettings.Password
$portBox.Text = [string]$savedSettings.ManagerPort
$subnetBox.Text = [string]$savedSettings.BoxSubnet

function Update-Status {
    param([string]$Message)
    $statusBlock.Text = $Message
}

function Get-CurrentSettingsObject {
    $port = 0
    [void][int]::TryParse($portBox.Text.Trim(), [ref]$port)
    [pscustomobject]@{
        Ssid = $ssidBox.Text.Trim()
        Password = $passwordBox.Password
        ManagerPort = $port
        BoxSubnet = $subnetBox.Text.Trim()
        ServerPid = [int]$savedSettings.ServerPid
    }
}

function Save-CurrentSettings {
    $settings = Get-CurrentSettingsObject
    Validate-Settings -Ssid $settings.Ssid -Password $settings.Password -ManagerPort $settings.ManagerPort -BoxSubnet $settings.BoxSubnet
    Save-Settings -Settings $settings
    $script:savedSettings = $settings
    $settings
}

function Open-ManagerUrl {
    $settings = Get-CurrentSettingsObject
    $url = "http://127.0.0.1:$($settings.ManagerPort)/"
    Start-Process $url | Out-Null
}

$showPasswordButton.Add_Click({
    [System.Windows.MessageBox]::Show(
        "Aktuelles Passwort:`n$($passwordBox.Password)",
        'AP-Passwort',
        'OK',
        'Information'
    ) | Out-Null
})

$saveButton.Add_Click({
    try {
        $settings = Save-CurrentSettings
        Update-Status "Einstellungen gespeichert: $settingsPath"
    }
    catch {
        [System.Windows.MessageBox]::Show($_.Exception.Message, 'Speichern fehlgeschlagen', 'OK', 'Warning') | Out-Null
        Update-Status "Fehler: $($_.Exception.Message)"
    }
})

$syncUsbButton.Add_Click({
    try {
        $settings = Save-CurrentSettings
        $written = Write-HotspotConfigToUsb -Settings $settings
        Update-Status "Hotspot-Konfiguration auf USB-Stick geschrieben:`n$($written -join "`n")"
    }
    catch {
        [System.Windows.MessageBox]::Show($_.Exception.Message, 'USB-Synchronisierung fehlgeschlagen', 'OK', 'Warning') | Out-Null
        Update-Status "Fehler: $($_.Exception.Message)"
    }
})

$startAllButton.Add_Click({
    try {
        $settings = Save-CurrentSettings

        if (-not (Test-IsAdministrator)) {
            $answer = [System.Windows.MessageBox]::Show(
                "Der AP-Start braucht Administratorrechte.`n`nSoll das Tool jetzt als Administrator neu gestartet werden?",
                'Administratorrechte erforderlich',
                'YesNo',
                'Question'
            )

            if ($answer -eq [System.Windows.MessageBoxResult]::Yes) {
                Restart-AsAdministrator
                $window.Close()
                return
            }

            throw 'AP-Start abgebrochen, weil Administratorrechte fehlen.'
        }

        try {
            $apOutput = Start-WindowsAccessPoint -Ssid $settings.Ssid -Password $settings.Password
        }
        catch {
            $fallbackAnswer = [System.Windows.MessageBox]::Show(
                "$($_.Exception.Message)`n`nSoll der Manager trotzdem gestartet und die Windows-Hotspot-Einstellungen geoeffnet werden?",
                'AP konnte nicht automatisch starten',
                'YesNo',
                'Warning'
            )

            if ($fallbackAnswer -ne [System.Windows.MessageBoxResult]::Yes) {
                throw
            }

            Start-Process 'ms-settings:network-mobilehotspot' | Out-Null
            $apOutput = 'AP wurde nicht automatisch gestartet. Windows-Hotspot-Einstellungen wurden geoeffnet.'
        }

        $serverPid = Start-ManagerServer -Settings $settings
        $settings.ServerPid = $serverPid
        Save-Settings -Settings $settings
        $script:savedSettings = $settings

        Update-Status "AP gestartet: $($settings.Ssid)`nManager laeuft auf http://127.0.0.1:$($settings.ManagerPort)/`n$apOutput"
        Open-ManagerUrl
    }
    catch {
        [System.Windows.MessageBox]::Show($_.Exception.Message, 'Start fehlgeschlagen', 'OK', 'Error') | Out-Null
        Update-Status "Fehler: $($_.Exception.Message)"
    }
})

$startManagerButton.Add_Click({
    try {
        $settings = Save-CurrentSettings
        $serverPid = Start-ManagerServer -Settings $settings
        $settings.ServerPid = $serverPid
        Save-Settings -Settings $settings
        $script:savedSettings = $settings

        Update-Status "Manager laeuft auf http://127.0.0.1:$($settings.ManagerPort)/`nAP wurde nicht gestartet."
        Open-ManagerUrl
    }
    catch {
        [System.Windows.MessageBox]::Show($_.Exception.Message, 'Manager-Start fehlgeschlagen', 'OK', 'Error') | Out-Null
        Update-Status "Fehler: $($_.Exception.Message)"
    }
})

$stopAllButton.Add_Click({
    try {
        $settings = Get-CurrentSettingsObject
        Stop-ManagerServer -SavedProcessId ([int]$settings.ServerPid)
        $settings.ServerPid = 0
        Save-Settings -Settings $settings
        $script:savedSettings = $settings

        if (Test-IsAdministrator) {
            $apOutput = Stop-WindowsAccessPoint
            Update-Status "Manager gestoppt. AP gestoppt.`n$apOutput"
        }
        else {
            Update-Status 'Manager gestoppt. AP konnte ohne Administratorrechte nicht gestoppt werden.'
        }
    }
    catch {
        [System.Windows.MessageBox]::Show($_.Exception.Message, 'Stopp fehlgeschlagen', 'OK', 'Error') | Out-Null
        Update-Status "Fehler: $($_.Exception.Message)"
    }
})

$openHotspotSettingsButton.Add_Click({
    Start-Process 'ms-settings:network-mobilehotspot' | Out-Null
})

Update-Status "Bereit. Startet den RiddleMatrix-AP und den lokalen Manager auf Port $($savedSettings.ManagerPort)."

$window.ShowDialog() | Out-Null
