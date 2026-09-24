# ForetoldRewards — POE2Fixer Plugin

Shows the foretold rewards of the Rite of the Nameless on the endgame atlas: for each map
that carries a ritual line, the rewards the rite can offer there, priced through the host's
price service, so you can plan which route to take before you run it.

## What It Does

- Reads the atlas through the host (`ctx->Atlas`: nodes, connections, selection) and
  computes each foretold reward on display from the line seed and the game's own reward
  pool. The near steps of a route match the game exactly; deep steps are estimates,
  shown with `~`, and a reward the game has already revealed replaces the estimate.
- Lists the rewards per map in its window, or on hover over a map node (Hover mode).
- Prices every reward with the host's price service (league and source are chosen in
  POEFixer Settings -> Prices).
- Map and biome filters: exact map names or `*substring*`, and per-biome toggles.
- A Performance tab with frame timings for troubleshooting.

## Settings

In the host Plugins settings tab: **Show window**, **Hover mode** and a **Debug overlay**
(with an option to show the coordinates of every map).

## Disclaimer

Community third-party plugin for POE2Fixer. Use at your own risk. Not affiliated with
Grinding Gear Games.

## Build from source

This repository is the plugin folder from the POEFixer source tree (`Plugins/ForetoldRewards/`) together with the host files it compiles against, kept at the same relative paths:

- `POEFixer/plugin_sdk/` - the POEFixer Plugin SDK v6 headers (`PluginAbi.h`, `PluginSDK.h`)
- `POEFixer/imgui/` - the Dear ImGui sources the DLL compiles against

1. Install Visual Studio 2022 with the "Desktop development with C++" workload (MSVC v143, Windows 10/11 SDK).
2. Open `ForetoldRewards.sln`, select **Release | x64** and build the solution.
3. The plugin is written to `x64\Release\Plugins\ForetoldRewards\ForetoldRewards.dll`.

## Install

Copy `ForetoldRewards.dll` into `Plugins\ForetoldRewards\` next to the POEFixer executable (create the folder if needed), start POEFixer and enable the plugin in the Plugins tab.
