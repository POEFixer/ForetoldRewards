# Changelog

## 1.0.2

- Works with the separate menu window (POEFixer 366): the host hands the plugin the right drawing context for its settings tab and for its overlay.
- The settings tab also sets its drawing context itself, so it draws in the right window with the menu in a separate window.
- Its window now has the POEFixer gear menu (POEFixer 366): lock it, take it out of the game onto any screen, reset its position or close it; its place and size are remembered separately in and out of the game.
- Pointing at the window out of the game, or at the POEFixer menu window lying over the atlas, no longer highlights or locks the map under it.
- Fixed: the chain list and the excluded-maps list no longer leave the window half-drawn when they are scrolled out of view.

## 1.0.1

- Rebuilt with the updated plugin SDK headers (POEFixer 365 adds optional calls for UI control names, stash objects and tabs, wider entity scans and the host automation flag). No change in what the plugin does.

## 1.0.0

- First versioned release.
