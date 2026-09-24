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
