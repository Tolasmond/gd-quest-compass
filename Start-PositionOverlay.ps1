$ErrorActionPreference = 'Stop'
$loader = Join-Path $PSScriptRoot 'build\position-loader.exe'
if (-not (Test-Path -LiteralPath $loader)) { throw 'Run build.cmd first.' }
$game = @(Get-Process -Name 'Grim Dawn' -ErrorAction SilentlyContinue)
if ($game.Count -ne 1) { throw 'Start the 64-bit game through Steam first. Exactly one Grim Dawn process must be running.' }
& $loader $game[0].Id
if ($LASTEXITCODE -ne 0) { throw "Position loader failed ($LASTEXITCODE)." }
