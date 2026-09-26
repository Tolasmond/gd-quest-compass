# GD Quest Compass Guide Recorder

Press **Ctrl+Shift+F8** in Grim Dawn to open the Recorder. It saves locations
and guide choices in `personal.json`, the public guide file. The Recorder uses
the overlay's selected tracked quest; press **Ctrl+Shift+F11** to change quests.
The game remains authoritative for quest progress. Guide edits do not change
quest completion or game saves.

## Record and reuse locations

1. Stand at the place you want to record and open the Recorder. Your position
   is captured when the window opens.
2. Choose an unfinished objective. Enter an optional location name and press
   **Record location**.
3. Close and reopen the Recorder after moving to capture another position.
   Linked waypoints appear in travel order. Select one and use the move buttons
   to change that order.

Select a saved location under **Other saved locations** and press **Use for
objective** to reuse it. **Unlink from objective** removes only that link.
Renaming, replacing, or deleting a location everywhere affects every objective
that uses it. **Undo last change** reverses the latest edit. **Reload guide**
rereads saved files without restarting the game.

For an interior destination, record an entrance approach first. After entering,
reopen the Recorder to capture an interior waypoint. The compass waits until
you enter the interior area before showing its waypoint bearing. It does not
provide obstacle-aware pathfinding.

## Other guidance

- **Live quest character:** Select a character from the Recorder's quest
  catalog and enable live tracking for the chosen objective. The selection is
  tied to the exact game record. When that character is nearby, its live
  position takes priority over saved waypoints. Missing or defeated enemies do
  not change quest completion.
- **Secret entrance:** Stand at the entrance, open the Recorder, set a radius
  if needed, and press **Record secret entrance**. This needs no quest
  objective. The yellow arrow appears only nearby in the recorded zone.
- **Outdoor crossing:** The Recorder may propose a connection after continuous
  travel across two outdoor zone labels. Review it and choose **Save selected
  connection** or **Dismiss proposal**. A saved connection permits bearings
  across those zones; it does not establish a walkable route between every pair
  of points.
- **Arrow visibility:** Quest, Shrine, and Secret checkboxes control their
  indicators independently. **Ctrl+Shift+F10** cycles the overall display mode.

## Files and recovery

| File | Purpose |
| --- | --- |
| `defaults.json` | Baseline guide definitions; the Recorder does not edit it |
| `personal.json` | Public recorded locations, links, and overrides |
| `quest-entities.json` | Quest character choices shown by the Recorder |
| `personal.json.bak` | Local recovery copy of the previous personal guide |
| `active-selection.json` | Local selected quest and objective |
| `observed-connections.json` | Local, unreviewed crossing proposals |

Recorder changes save immediately. `personal.json` is the file to carry forward
or contribute to the public guide. If an external edit changes the guide while
the Recorder is open, choose **Reload guide** before saving again.
