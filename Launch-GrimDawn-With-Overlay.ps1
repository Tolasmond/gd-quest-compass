param([switch]$Log)
$ErrorActionPreference = 'Stop'

$loader = Join-Path $PSScriptRoot 'build\position-loader.exe'
$overlay = Join-Path $PSScriptRoot 'build\position-overlay.dll'
if (-not (Test-Path -LiteralPath $loader) -or -not (Test-Path -LiteralPath $overlay)) {
    throw 'Overlay files are missing from build/. Re-extract the package or build from source.'
}
$games = @(Get-Process -Name 'Grim Dawn' -ErrorAction SilentlyContinue)
if ($games.Count -gt 1) { throw 'More than one Grim Dawn process is running; close the extra instance first.' }
if ($games.Count -eq 0) {
    Write-Host 'Starting Grim Dawn through Steam...'
    Start-Process 'steam://rungameid/219990'
} else {
    Write-Host 'Grim Dawn is already running; waiting for it to be ready...'
}

$deadline = (Get-Date).AddMinutes(2)
$readySince = $null
$game = $null
while ((Get-Date) -lt $deadline) {
    $games = @(Get-Process -Name 'Grim Dawn' -ErrorAction SilentlyContinue)
    if ($games.Count -gt 1) { throw 'More than one Grim Dawn process is running; overlay not loaded.' }
    if ($games.Count -eq 1) {
        $game = $games[0]
        try {
            $modules = @($game.Modules | ForEach-Object { $_.ModuleName })
            if ($modules -contains 'position-overlay.dll') {
                Write-Host 'Overlay is already loaded in this game process.'
                exit 0
            }
            $ready = $game.MainWindowHandle -ne [IntPtr]::Zero -and
                $modules -contains 'Game.dll' -and $modules -contains 'Engine.dll'
            if ($ready) {
                if (-not $readySince) { $readySince = Get-Date }
                if (((Get-Date) - $readySince).TotalSeconds -ge 5) { break }
            } else { $readySince = $null }
        } catch [System.ComponentModel.Win32Exception] {
            $readySince = $null
        } catch [System.InvalidOperationException] {
            $readySince = $null
        }
    }
    Start-Sleep -Milliseconds 500
}
if (-not $readySince -or ((Get-Date) - $readySince).TotalSeconds -lt 5) {
    throw 'Timed out waiting for the Grim Dawn window and game modules. Overlay was not loaded.'
}

if ($Log) {
    # A logging launch overwrites its log; retain the preceding session.
    $logPath = Join-Path $PSScriptRoot 'build\position-overlay.log'
    if (Test-Path -LiteralPath $logPath) {
        $archive = Join-Path $PSScriptRoot 'build\log-archive'
        New-Item -ItemType Directory -Path $archive -Force | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        Copy-Item -LiteralPath $logPath -Destination (Join-Path $archive "position-overlay-$stamp.log")
    }
}

Write-Host 'Loading the overlay...'
if ($Log) { & $loader --log $game.Id }
else { & $loader $game.Id }
if ($LASTEXITCODE -ne 0) { throw "Position loader failed ($LASTEXITCODE)." }
Write-Host 'Ready. Load a character whenever you like.'
