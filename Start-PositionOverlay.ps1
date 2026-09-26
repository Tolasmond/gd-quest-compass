param([switch]$Log)
$ErrorActionPreference = 'Stop'
$loader = Join-Path $PSScriptRoot 'build\position-loader.exe'
if (-not (Test-Path -LiteralPath $loader)) { throw 'Overlay loader is missing from build/. Re-extract the package or build from source.' }
$game = @(Get-Process -Name 'Grim Dawn' -ErrorAction SilentlyContinue)
if ($game.Count -ne 1) { throw 'Start the 64-bit game through Steam first. Exactly one Grim Dawn process must be running.' }
if ($Log) {
    $logPath = Join-Path $PSScriptRoot 'build\position-overlay.log'
    if (Test-Path -LiteralPath $logPath) {
        $archive = Join-Path $PSScriptRoot 'build\log-archive'
        New-Item -ItemType Directory -Path $archive -Force | Out-Null
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        Copy-Item -LiteralPath $logPath -Destination (Join-Path $archive "position-overlay-$stamp.log")
    }
    & $loader --log $game[0].Id
} else { & $loader $game[0].Id }
if ($LASTEXITCODE -ne 0) { throw "Position loader failed ($LASTEXITCODE)." }
