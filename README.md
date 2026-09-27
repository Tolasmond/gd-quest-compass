# gd-quest-compass

GD Quest Compass is a Windows overlay for repeat Grim Dawn playthroughs.
Using GD Quest Compass does not make any additions or modifications to the
installed Grim Dawn Steam files.

Status: I have only played through the original campaign while making
guide recordings. No other guidance is available at this time.

GD Quest Compass reads the current character's position, tracked quests, and quest progress,
then points toward saved guide locations. The guide includes quest waypoints,
nearby devotion shrines, and recorded secret entrances. Bearings show direction;
they do not navigate around walls or choose a walkable path.

## Why?
I have played the first three expansions thoroughly and generally have an idea where
to go for most of the quests. I'm lazy/impatient with the new expansion and would like
guidance on where to go without having to look at ad-filled online wikis. I don't want
to have to depend on my memory for future playthroughs. I'd rather take notes once and
be guided with future characters any time my memory fails me.

I do NOT encourage others to play this way as it is more immersive to play without
guides. However, for single-player offline games, I believe players should be able to
play their games however they want.

## Disclaimer
This project was built using AI agent(s). I have not looked at the code as this is just a game
and I care only about the results. I opened up Codex and asked if it was possible and it
found two repos (see Acknowledgements at the end) that helped provide enough functionality
that I was willing to put in the time to fill in missing data as I play.

Comments, suggestions, bug-reports, etc. are welcome. However, this project is low-priority for
me so don't expect quick responses to any communications.

## Use a prebuilt package

To use a packaged build, you need Windows x64, Grim Dawn installed through
Steam with its 64-bit executable at `<game directory>\x64\Grim Dawn.exe`.
The binaries use the Microsoft Visual C++ runtime. If a compatible x64 runtime
is not already installed, install the
[Microsoft Visual C++ x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170).
Visual Studio and the Windows SDK are only needed to compile the project.
Extract the package to a writable folder, preserving this layout:

```text
gd-quest-compass/
  README.md
  LICENSE
  THIRD_PARTY_NOTICES.md
  Launch-GrimDawn-With-Overlay.cmd
  Launch-GrimDawn-With-Overlay.ps1
  Start-PositionOverlay.ps1
  build/
    position-loader.exe
    position-overlay.dll
  data/guide/
    README.md
    defaults.json
    personal.json
    quest-entities.json
  vendor/Detours/
    LICENSE
```

The Windows build workflow provides `gd-quest-compass-windows-x64.zip` as a
download on its successful Actions run. Extract that ZIP before launching.

## Build from source

To build from source, clone this repository. Building requires Visual Studio
with the MSVC C++ x64 tools and a Windows SDK.
`setup-msvc.cmd` finds the installed toolchain through Visual Studio Installer's
`vswhere`. Run `build.cmd` from a Windows command prompt; it produces
`build/position-overlay.dll` and `build/position-loader.exe`. Then use the
launch instructions above.

Exit Grim Dawn before rebuilding `build/position-overlay.dll`; Windows keeps the
injected DLL locked while the game is running.

## Running and diagnostics

Run `Launch-GrimDawn-With-Overlay.cmd`. It starts Grim Dawn through Steam if
needed, waits for the game window and required modules, and loads the overlay.
If one game instance is already running, it uses that instance. The loader
finds the installation path from the running process.

The loader verifies that the target process is a 64-bit `x64/Grim Dawn.exe`.
The overlay checks the installed `Game.dll` and `Engine.dll` before installing
its hooks. It currently accepts only the game binaries inspected for this
version of the project. If the game has updated, the loader refuses to hook it
until compatibility is checked and the accepted fingerprints are updated.

The Recorder saves changes to `data/guide/personal.json` and writes local
recovery and session files beside it, so the guide folder must be writable.

The overlay stays loaded until the game exits. Diagnostic logging is off by
default. To record a session for troubleshooting, run
`Launch-GrimDawn-With-Overlay.cmd -Log`. The log is written to
`build/position-overlay.log` in the extracted package; a previous log is
archived before a new logging session.

For a game already running, `Start-PositionOverlay.ps1` is a manual loader;
pass `-Log` there for diagnostics. The one-click launcher performs the extra
readiness checks.

## Controls

Use these hotkeys while the game is active:

| Hotkey | Action |
| --- | --- |
| Ctrl+Shift+F7 | Record the current position for the active unfinished quest objective while the Recorder is closed |
| Ctrl+Shift+F8 | Open the Guide Recorder |
| Ctrl+Shift+F9 | Choose the next eligible approach or target |
| Ctrl+Shift+F10 | Cycle triangle only, triangle plus panel, and hidden |
| Ctrl+Shift+F11 | Choose the next tracked quest |
| Ctrl+Shift+F12 | Show the next quest-details page |

The green triangle points toward the selected quest destination. Yellow marks a
nearby recorded secret. Cyan marks a nearby shrine/totem.

To quickly add a waypoint for the active objective, stand at the location and
press **Ctrl+Shift+F7** with the Recorder closed. It captures the current
position and saves immediately. To choose a different unfinished objective or
name the location, open the Guide Recorder and press **Record location**. The
Recorder captures the position when it opens; close and reopen it after moving.
You can link an
existing location, reorder linked waypoints, undo a change, and reload the guide
without rebuilding. Outdoor crossing proposals require review before they are
saved as guide connections. See [Guide Recorder usage](data/guide/README.md).

Quest completion comes from the game's quest state. An enemy's appearance,
death, or disappearance alone does not complete or reactivate an objective.

## Guide files

- `data/guide/defaults.json`: baseline guide definitions informed by extracted
  game data and reviewed mappings.
- `data/guide/personal.json`: public recorded locations, links, and overrides.
- `data/guide/quest-entities.json`: character choices used by the Recorder.

The Recorder writes changes to `personal.json` and creates a local `.bak`
recovery copy. Active selection and unreviewed crossing proposals are stored
separately as local session state. See [Guide Recorder usage](data/guide/README.md)
for editing and recovery details.

## Acknowledgments

This project would not have been possible without
[nonoroazoro's gd-cli](https://github.com/nonoroazoro/gd-cli). Its game-data
queries made the guide's quest, entity, and location research possible. The
published guide also includes separately reviewed and recorded locations.

The position-reading approach is adapted from
[Grimdark](https://github.com/ahicks92/grimdark). Microsoft Detours source is
included under `vendor/Detours`. See [third-party notices](THIRD_PARTY_NOTICES.md)
and the [Detours license](vendor/Detours/LICENSE).

## License

GD Quest Compass is released under the [MIT License](LICENSE). Third-party
components keep their own licenses; see [third-party notices](THIRD_PARTY_NOTICES.md).
