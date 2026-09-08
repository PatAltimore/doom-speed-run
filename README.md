# doom-speed-run

A browser-playable speed-running game built on the original 1993 *DOOM* (shareware episode, "Knee-Deep in the Dead"). Pick a challenge, beat the clock, and text or email your time to a friend so they can try to beat it.

Derived from [doom-assist](https://github.com/PatAltimore/doom-assist): same engine, same twin-stick touch controls and keyboard play, with the assist-mode sidebar (map, cheats, code links) replaced by a run timer and a challenge board.

## How it works

- **Challenges**: about 130, generated from the 9 shareware maps (plus E1M3's secret exit) crossed with 16 categories and tagged Beginner / Intermediate / Hard. Twelve are DSDA categories (NoMo, NoMo 100S, UV Speed, Pacifist, Collector, Stroller, UV Fast, UV Respawn, NM Speed, NM 100S, UV Max, Tyson); four are this game's own stepping stones (Easy Speed, HMP Speed, UV 100S, Untouched). Each DSDA card shows the human world record from [DSDA](https://dsdarchive.com/wads/doom) and links to its leaderboard. A combination is only offered when DSDA has a human record for it under 3:30 (Stroller on the big maps runs 18 to 27 minutes) and it doesn't depend on an out-of-bounds trick (E1M8 without killing the Barons) — plus one named exception, E1M3's secret-exit Pacifist (3:40.83), which DSDA tracks separately from the normal-exit route the same way it does for UV Speed and NM Speed. The time limit is four times the record, rounded up to a half minute, between 2:00 and 10:00.
- **Daily challenge**: one challenge is chosen per UTC day, deterministically from the day number (a seeded shuffle that plays every challenge once per cycle), so everyone gets the same one. Any challenge can also be picked from the full list.
- **Timer**: in-game time from the engine's own `leveltime` tic counter (35 tics/sec), displayed DSDA-style as `m:ss.hh`. It starts when the level loads and stops the tic the exit triggers.
- **Rules are judged by the engine**, not the page: Pacifist, Stroller, and Tyson watch `P_DamageMobj` for player-inflicted monster damage (barrels and telefrags exempt, per DSDA rules), Untouched watches it for damage to the player; Stroller also counts run-speed and strafe tics in `G_BuildTiccmd`; Max/100S/Collector/Fast/Respawn compare kill, secret, and item counts against the level totals at exit; NoMo, UV Fast, and UV Respawn set the engine's `-nomonsters`/`-fast`/`-respawn` flags for the run; the E1M3 challenges check which exit was taken. Dying, running out of time, or breaking a category rule ends the attempt. Vanilla cheat codes are compiled out.
- **Restart**: the ↺ button in the timer bar, or Backspace (unused by vanilla DOOM outside its menus), aborts the current attempt and restarts it from the 3-2-1 countdown. Enter starts a challenge from its start dialog.
- **Free play**: a panel section to warp to any map at any skill (optionally with no monsters) and just play, untimed; exiting carries on into the next level.
- **Sharing**: after a run (or from the panel, for all your bests) — the system share sheet where available, plus Text (`sms:`), Email (`mailto:`), and Copy. The link carries the challenge and your time (`?c=<id>&t=<tics>`), so the friend opens straight into it with "time to beat" showing.
- **Your times** for every completed challenge (best, attempt count, recent runs) are kept in the browser's `localStorage`.

## What's here

- `engine/` — [ozkl/doomgeneric](https://github.com/ozkl/doomgeneric) (a portability fork of id Software's [linuxdoom-1.10](https://github.com/id-Software/DOOM/tree/master/linuxdoom-1.10) source with an Emscripten target), vendored flat with a few small local patches marked `--- doom-speed-run patch ---` inline: `g_game.c` (touch input into `G_BuildTiccmd`, next-weapon, level-completed hook, NoMo/Fast/Respawn flags in `G_DoNewGame`, a fast-monster toggle fix in `G_InitNew`), `d_main.c` (no screen-melt on a challenge start), `p_inter.c` (damage hook), `p_setup.c` (level-start hook), `st_stuff.c` (cheats gated off), `m_menu.c` (tap-to-select menu items).
- `engine/speedrun.c` — the exports `web/shell.html` calls: start a challenge, read the clock and counters, the rule trackers, and the twin-stick touch controls.
- `web/shell.html` — the custom Emscripten shell: timer bar, challenge panel, results overlay, sharing, touch/keyboard controls.
- `data/shareware/DOOM1.WAD` — the original 1995 shareware episode data (v1.9). id Software has permitted free redistribution of this file since the game shipped as shareware; the full registered game's data is not included.
- `staticwebapp.config.json`, `.github/workflows/azure-static-web-apps.yml` — Azure Static Web Apps deployment.

## Running locally

```bash
# 1. Get the Emscripten SDK (one-time; ~1GB, not checked into the repo)
git clone https://github.com/emscripten-core/emsdk.git tools/emsdk
cd tools/emsdk && python emsdk.py install 6.0.6 && python emsdk.py activate 6.0.6 && cd ../..

# 2. Activate it in your shell (on Windows Git Bash, point EMSDK_PYTHON at
#    the SDK's bundled python first if the system python isn't picked up)
source tools/emsdk/emsdk_env.sh

# 3. Build the engine
cd engine && bash build-emscripten.sh && cd ..

# 4. Serve it (browsers block wasm/fetch from file:// URLs)
cd engine && python -m http.server 8092

# 5. Open http://localhost:8092/index.html
```

## Deployment

Pushing to `main` triggers `.github/workflows/azure-static-web-apps.yml`, which builds the engine fresh in CI and deploys the result to Azure Static Web Apps using the `AZURE_STATIC_WEB_APPS_API_TOKEN` repo secret (create the Static Web App and add that secret before the first push).

## License / credits

- DOOM source: id Software, released under GPL-2.0 in 1999 (see `engine/license-gpl.txt`). This project is personal and non-commercial.
- Engine portability layer: [doomgeneric](https://github.com/ozkl/doomgeneric), GPL-2.0.
- Shareware game data: id Software, freely redistributable since 1993.
- World-record times and category definitions: the [Doom Speed Demo Archive](https://dsdarchive.com/) community.
