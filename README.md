# ForetoldRewards — POE2Fixer Plugin

Current version: 1.0.3

Requires POE2Fixer 366 or newer.

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

## Install

Install it from POE2Fixer → Plugins → Plugin Manager (one click, updates included), or download `ForetoldRewards.zip` from [Releases](https://github.com/POEFixer/ForetoldRewards/releases/latest) and extract it into the POE2Fixer folder (it creates `Plugins\ForetoldRewards\`). Then enable the plugin in the Plugins tab.

## Build from source

This repository is the plugin folder from the POE2Fixer source tree (`Plugins/ForetoldRewards/`) together with the host files it compiles against, kept at the same relative paths:

- `POEFixer/plugin_sdk/` - the POE2Fixer Plugin SDK v6 headers (`PluginAbi.h`, `PluginSDK.h`)
- `POEFixer/imgui/` - the Dear ImGui sources the DLL compiles against
- `tools/plugin_i18n/gen.py` - the translation tool: after editing `Plugins/ForetoldRewards/lang/en.json` run `python tools/plugin_i18n/gen.py Plugins/ForetoldRewards` (it rewrites the embedded English header); `--gate` checks every language file and the keys the sources use

1. Install Visual Studio 2022 with the "Desktop development with C++" workload (MSVC v143, Windows 10/11 SDK).
2. Open `ForetoldRewards.sln`, select **Release | x64** and build the solution.
3. The plugin is written to `x64\Release\Plugins\ForetoldRewards\ForetoldRewards.dll` (Release builds carry no debug information, see `Directory.Build.targets`).
4. Copy `ForetoldRewards.dll` into `Plugins\ForetoldRewards\` next to the POE2Fixer executable, together with `Plugins\ForetoldRewards\lang` from this repository (same folder names).
