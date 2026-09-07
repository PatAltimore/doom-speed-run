//
// speedrun.c -- doom-speed-run's browser support code.
//
// Everything in this file is new: it's not part of doomgeneric or the
// original id Software source. It exists to (a) let web/shell.html start
// a timed challenge and read the engine's own clock and progress
// counters, (b) judge the DSDA-style category rules (Pacifist, Tyson,
// Max, 100S) from inside the engine where the facts actually are, and
// (c) accept the twin-stick touch input back from the page.
//
// Design note: almost nothing here computes anything new. DOOM already
// keeps an in-game tic clock (leveltime -- the same clock DSDA times
// are measured in), already counts kills/secrets per player and per
// level, and already has a single choke point every source of damage
// goes through (P_DamageMobj). Each speedrun_* function below is a thin
// EMSCRIPTEN_KEEPALIVE reader over a global the engine already maintains,
// or a hook called from one of the handful of one-line patches marked
// `--- doom-speed-run patch ---` in the vanilla source.

#include <stdio.h>
#include <string.h>
#include <emscripten.h>

#include "doomdef.h"    // gamestate_t, GS_LEVEL, skill_t
#include "doomstat.h"   // gameepisode, gamemap, gameskill, paused, players[], leveltime, totalkills, totalsecret
#include "d_player.h"   // player_t, PST_DEAD
#include "g_game.h"     // G_DeferedInitNew
#include "p_mobj.h"     // mobj_t, MF_COUNTKILL, MF_MISSILE
#include "info.h"       // MT_BARREL, MT_SKULL
#include "s_sound.h"    // S_ResumeSound

// -----------------------------------------------------------------------
// Basic state readers (shared with the touch-control UI)
// -----------------------------------------------------------------------
// gameepisode/gamemap are set whenever a level starts and hold their
// last value afterwards, so the page can poll them at any time.
EMSCRIPTEN_KEEPALIVE int speedrun_get_episode(void) { return gameepisode; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_map(void) { return gamemap; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_skill(void) { return (int)gameskill; }

// GS_LEVEL means "actually playing a map right now" (as opposed to a
// menu, intermission screen, or the demo loop).
EMSCRIPTEN_KEEPALIVE int speedrun_get_gamestate(void) { return (int)gamestate; }

// player_t fields the engine already maintains for the HUD/intermission
// screen (st_stuff.c, wi_stuff.c), plus the level totals P_SetupLevel
// counts while spawning things -- the same numbers the intermission
// screen's percentages are computed from.
EMSCRIPTEN_KEEPALIVE int speedrun_get_kills(void) { return players[consoleplayer].killcount; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_items(void) { return players[consoleplayer].itemcount; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_secrets(void) { return players[consoleplayer].secretcount; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_total_kills(void) { return totalkills; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_total_secrets(void) { return totalsecret; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_total_items(void) { return totalitems; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_weapon(void) { return (int)players[consoleplayer].readyweapon; }

// The run clock. leveltime (p_tick.c) counts game tics (35/sec) since
// the level loaded, stops while paused, and is exactly what DSDA's
// demo times are measured in -- so the page never needs its own clock,
// which would drift from the engine on a slow frame anyway.
EMSCRIPTEN_KEEPALIVE int speedrun_get_leveltime(void) { return leveltime; }

// PST_DEAD is set by P_KillMobj the tic the player dies and cleared on
// reborn (G_DoReborn -> P_SpawnPlayer).
EMSCRIPTEN_KEEPALIVE int speedrun_is_dead(void)
{
    return players[consoleplayer].playerstate == PST_DEAD ? 1 : 0;
}

// The engine's own pause flag (see G_Ticker/BTS_PAUSE in g_game.c).
EMSCRIPTEN_KEEPALIVE int speedrun_is_paused(void) { return paused ? 1 : 0; }

// G_Responder sets sendpause on the Pause keydown and G_Ticker toggles
// `paused` from it next tic; setting the flag directly skips the
// keyboard round-trip (the Emscripten key translation has no Pause).
EMSCRIPTEN_KEEPALIVE void speedrun_toggle_pause(void)
{
    extern boolean sendpause; // g_game.c
    sendpause = true;
}

// Lets the page tell "actually playing" apart from "a menu is drawn over
// whatever's behind it" -- menuactive doesn't stop gameplay simulating
// behind it (m_menu.c). The touch controls swap Fire/Use for a Back
// button whenever a menu might be showing.
EMSCRIPTEN_KEEPALIVE int speedrun_get_menuactive(void)
{
    extern boolean menuactive; // m_menu.c
    return menuactive ? 1 : 0;
}

// usergame (doomstat.h) is true only for a game a person is actually
// playing: G_InitNew sets it, and both the attract-mode demo player
// (G_DoPlayDemo) and the title/finale states clear it. Together with
// demoplayback below it's how the page knows a fresh level load is a
// real attempt and not the title screen cycling through its demos.
EMSCRIPTEN_KEEPALIVE int speedrun_get_usergame(void) { return usergame ? 1 : 0; }

// The attract-mode demo runs as a completely real GS_LEVEL (a level plus
// a prerecorded input stream), so gamestate alone can't tell "playing"
// from "watching the title-screen demo".
EMSCRIPTEN_KEEPALIVE int speedrun_get_demoplayback(void)
{
    return demoplayback ? 1 : 0;
}

// -----------------------------------------------------------------------
// Cheats
// -----------------------------------------------------------------------
// Gate for the cheat-code block in ST_Responder (st_stuff.c, see the
// doom-speed-run patch there). Always off: iddqd/idkfa/idclev would make
// a timed competition meaningless. A function rather than a deleted
// block so the vanilla source stays diffable against upstream.
int speedrun_cheats_enabled(void) { return 0; }

// -----------------------------------------------------------------------
// Starting a challenge
// -----------------------------------------------------------------------
// G_DeferedInitNew (g_game.c) is exactly what the Skill menu and the
// IDCLEV cheat both call: it records skill/episode/map and sets
// gameaction = ga_newgame, which G_Ticker turns into G_DoNewGame ->
// G_InitNew -> G_DoLoadLevel on the next tic. That path already handles
// every state we might be called from -- the title-screen demo loop
// (demoplayback is cleared), an open menu, an intermission, a dead
// player -- because those are the same states the real menu can start a
// new game from. leveltime resets to 0 in P_SetupLevel, so the run
// clock starts exactly when the level does, like a DSDA demo's.
//
// nomonsters: DSDA's "NoMo" category is the map with no monsters at all
// (-nomonsters), which is the natural beginner tier -- pure route
// practice. The engine's `nomonsters` global is honoured by
// P_SpawnMapThing, but G_DoNewGame unconditionally clears it before
// every new game, so the request is parked here and G_DoNewGame (see
// its doom-speed-run patch) re-reads it at exactly the point it would
// otherwise clear it. Reborn-after-death reloads keep the flag as-is,
// so a retry stays monster-free too.
//
// fast / respawn: DSDA's UV Fast and UV Respawn categories are UV Max
// with the -fast or -respawn switch. Same parking trick as nomonsters;
// G_InitNew then applies them exactly as the command-line flags would
// (see the fast-monster toggle patch there).
#define SPEEDRUN_FLAG_NOMONSTERS 1
#define SPEEDRUN_FLAG_FAST       2
#define SPEEDRUN_FLAG_RESPAWN    4
#define SPEEDRUN_FLAG_KEEP_WIPE  8   // free play: no countdown to sync with, so keep the classic melt
static int speedrun_nomonsters_flag, speedrun_fast_flag, speedrun_respawn_flag;
int speedrun_nomonsters_requested(void) { return speedrun_nomonsters_flag; }
int speedrun_fast_requested(void) { return speedrun_fast_flag; }
int speedrun_respawn_requested(void) { return speedrun_respawn_flag; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_fastparm(void) { return fastparm ? 1 : 0; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_respawnparm(void) { return respawnparm ? 1 : 0; }

// One-shot "don't melt into this level" request, consumed by D_Display
// (see its doom-speed-run patch) on the first frame whose gamestate
// differs from the last drawn one -- i.e. the frame the new level
// first appears. Armed by speedrun_start only, so menu-started games
// and every later transition keep the classic wipe.
static int speedrun_skip_wipe_flag;
int speedrun_consume_skip_wipe(void)
{
    int v = speedrun_skip_wipe_flag;
    speedrun_skip_wipe_flag = 0;
    return v;
}
EMSCRIPTEN_KEEPALIVE int speedrun_get_nomonsters(void) { return nomonsters ? 1 : 0; }

// flags: a bitmask of SPEEDRUN_FLAG_* above (mirrored by F_* in shell.html).
EMSCRIPTEN_KEEPALIVE void speedrun_start(int skill, int episode, int map, int flags)
{
    extern void M_ClearMenus(void); // m_menu.c

    if (skill < (int)sk_baby) skill = (int)sk_baby;
    if (skill > (int)sk_nightmare) skill = (int)sk_nightmare;
    speedrun_nomonsters_flag = (flags & SPEEDRUN_FLAG_NOMONSTERS) ? 1 : 0;
    speedrun_fast_flag = (flags & SPEEDRUN_FLAG_FAST) ? 1 : 0;
    speedrun_respawn_flag = (flags & SPEEDRUN_FLAG_RESPAWN) ? 1 : 0;
    speedrun_skip_wipe_flag = (flags & SPEEDRUN_FLAG_KEEP_WIPE) ? 0 : 1;

    // A menu left open on top of a fresh level would swallow the first
    // keypresses; G_InitNew itself already un-pauses (paused = false +
    // S_ResumeSound), so pause needs no handling here.
    M_ClearMenus();
    G_DeferedInitNew((skill_t)skill, episode, map);
}

// -----------------------------------------------------------------------
// Per-attempt rule counters
// -----------------------------------------------------------------------
// Reset on every level load (speedrun_on_level_start, called from the
// P_SetupLevel patch) so a retry never inherits the previous attempt's
// violations, and bumped from the P_DamageMobj patch as damage happens.
//
// monster_hits: any damage the player dealt to a monster, except what
// the DSDA Pacifist rules explicitly allow -- barrel explosions (the
// barrel is the inflictor, the player only the source) and telefrags
// (the 10000-damage P_TeleportMove kill). A single hit invalidates a
// Pacifist run, so the page shows this the moment it becomes nonzero.
//
// nontyson_hits: player damage to a monster from anything other than
// the fist, pistol, or chainsaw. Projectile weapons are recognised by
// their missile (rockets, plasma, BFG all arrive with an MF_MISSILE
// inflictor); hitscan and melee arrive with the player as the inflictor,
// where readyweapon is still the weapon that just fired. Barrel splash
// is allowed in Tyson too (same inflictor check).
//
// player_damage: every point of damage the player took, from anything
// (monsters, barrels, their own rockets, nukage floors, crushers) -- the
// "Untouched" category. Counted from the same P_DamageMobj hook, before
// armour absorbs any of it, so a single graze counts.
//
// run_tics / strafe_tics: tics in which the built ticcmd asked for more
// than walking speed forward/back, or any sideways movement at all --
// the "Stroller" category (walk only, no strafing). Counted in
// speedrun_apply_touch_controls, which G_BuildTiccmd calls after every
// input source has contributed and before the clamp. Walking speed is
// forwardmove[0] == 25; the run key, a full touch push, or SR40-style
// key combos all exceed it.
static int speedrun_level_loads;   // total P_SetupLevel calls this session
static int speedrun_monster_hits;
static int speedrun_nontyson_hits;
static int speedrun_player_damage;
static int speedrun_run_tics;
static int speedrun_strafe_tics;

void speedrun_on_level_start(void)
{
    speedrun_level_loads++;
    speedrun_monster_hits = 0;
    speedrun_nontyson_hits = 0;
    speedrun_player_damage = 0;
    speedrun_run_tics = 0;
    speedrun_strafe_tics = 0;
}

void speedrun_on_damage(mobj_t *target, mobj_t *inflictor, mobj_t *source, int damage)
{
    if (target->player && target->player == &players[consoleplayer])
        speedrun_player_damage += damage;       // Untouched: anything that hurts us counts
    if (!source || !source->player)
        return;                                 // monster-on-monster, crushers, nukage: not the player's doing
    if (target == source || target->player)
        return;                                 // self-damage (rocket splash) isn't hurting a monster
    if (!(target->flags & MF_COUNTKILL) && target->type != MT_SKULL)
        return;                                 // barrels, decorations -- only monsters count
    if (damage >= 10000)
        return;                                 // telefrag: allowed under Pacifist rules
    if (inflictor && inflictor->type == MT_BARREL)
        return;                                 // barrel splash: allowed under Pacifist and Tyson rules

    speedrun_monster_hits++;

    if (inflictor && (inflictor->flags & MF_MISSILE))
    {
        speedrun_nontyson_hits++;               // rocket / plasma / BFG
        return;
    }
    switch (source->player->readyweapon)
    {
        case wp_fist:
        case wp_pistol:
        case wp_chainsaw:
            break;
        default:
            speedrun_nontyson_hits++;           // shotgun / chaingun hitscan
            break;
    }
}

EMSCRIPTEN_KEEPALIVE int speedrun_get_level_loads(void) { return speedrun_level_loads; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_monster_hits(void) { return speedrun_monster_hits; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_nontyson_hits(void) { return speedrun_nontyson_hits; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_player_damage(void) { return speedrun_player_damage; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_run_tics(void) { return speedrun_run_tics; }
EMSCRIPTEN_KEEPALIVE int speedrun_get_strafe_tics(void) { return speedrun_strafe_tics; }

// -----------------------------------------------------------------------
// Level completion
// -----------------------------------------------------------------------
// Called from the G_DoCompleted patch (g_game.c) on the tic the level
// ends -- before the intermission starts and before anything resets.
// Everything the page needs to judge and record the run is snapshotted
// here, because by the time the page polls, the player may already have
// tapped through the intermission and P_SetupLevel will have zeroed
// leveltime and the counters for the next map.
static struct
{
    int count;          // completions this session; the page diffs this to notice a new one
    int time;           // leveltime at exit, in tics
    int secret_exit;    // 1 if the secret exit was taken
    int kills, total_kills;
    int secrets, total_secrets;
    int items;
    int monster_hits, nontyson_hits;
    int skill, episode, map;
    int nomonsters, fast, respawn;
    int total_items;
    int player_damage, run_tics, strafe_tics;
} speedrun_result;

void speedrun_on_level_completed(int secret_exit)
{
    speedrun_result.count++;
    speedrun_result.time = leveltime;
    speedrun_result.secret_exit = secret_exit;
    speedrun_result.kills = players[consoleplayer].killcount;
    speedrun_result.total_kills = totalkills;
    speedrun_result.secrets = players[consoleplayer].secretcount;
    speedrun_result.total_secrets = totalsecret;
    speedrun_result.items = players[consoleplayer].itemcount;
    speedrun_result.monster_hits = speedrun_monster_hits;
    speedrun_result.nontyson_hits = speedrun_nontyson_hits;
    speedrun_result.skill = (int)gameskill;
    speedrun_result.episode = gameepisode;
    speedrun_result.map = gamemap;
    speedrun_result.nomonsters = nomonsters ? 1 : 0;
    speedrun_result.fast = fastparm ? 1 : 0;
    speedrun_result.respawn = respawnparm ? 1 : 0;
    speedrun_result.total_items = totalitems;
    speedrun_result.player_damage = speedrun_player_damage;
    speedrun_result.run_tics = speedrun_run_tics;
    speedrun_result.strafe_tics = speedrun_strafe_tics;
}

EMSCRIPTEN_KEEPALIVE int speedrun_get_completions(void) { return speedrun_result.count; }

// One indexed getter instead of a dozen exports. Field numbers are
// mirrored by RESULT_FIELDS in web/shell.html.
EMSCRIPTEN_KEEPALIVE int speedrun_get_result(int field)
{
    switch (field)
    {
        case 0:  return speedrun_result.time;
        case 1:  return speedrun_result.secret_exit;
        case 2:  return speedrun_result.kills;
        case 3:  return speedrun_result.total_kills;
        case 4:  return speedrun_result.secrets;
        case 5:  return speedrun_result.total_secrets;
        case 6:  return speedrun_result.items;
        case 7:  return speedrun_result.monster_hits;
        case 8:  return speedrun_result.nontyson_hits;
        case 9:  return speedrun_result.skill;
        case 10: return speedrun_result.episode;
        case 11: return speedrun_result.map;
        case 12: return speedrun_result.nomonsters;
        case 13: return speedrun_result.total_items;
        case 14: return speedrun_result.player_damage;
        case 15: return speedrun_result.run_tics;
        case 16: return speedrun_result.strafe_tics;
        case 17: return speedrun_result.fast;
        case 18: return speedrun_result.respawn;
        default: return 0;
    }
}

// -----------------------------------------------------------------------
// Twin-stick touch controls
// -----------------------------------------------------------------------
// Two independent sticks (web/shell.html): a left one for movement
// (forward/back and strafe left/right) and a right one for turning.
// Firing isn't handled here at all -- the right stick's touch handler
// dispatches the real Fire key (Control) on pointerdown/pointerup, the
// same synthetic-keyboard trick the Use/Back buttons use.
//
// Strafe and turn are scaled to 60% of their run-speed keyboard
// equivalents (sidemove[1]==40 -> 24, angleturn[1]==1280 -> 768) because
// the short throw of a touch stick made a full push feel twitchy on
// those axes; forward/back maps a full push to exactly forwardmove[1]
// (50), i.e. holding the run key. See G_BuildTiccmd's patch site in
// g_game.c: these add straight into its local forward/side/angleturn
// after every other input source, before the shared clamp.
static int speedrun_move_dx = 0, speedrun_move_dy = 0; // -100..100, left stick
static int speedrun_turn_dx = 0;                       // -100..100, right stick

EMSCRIPTEN_KEEPALIVE void speedrun_set_move(int dx, int dy)
{
    speedrun_move_dx = dx;
    speedrun_move_dy = dy;
}

EMSCRIPTEN_KEEPALIVE void speedrun_set_turn(int dx)
{
    speedrun_turn_dx = dx;
}

// Not EMSCRIPTEN_KEEPALIVE: only called from G_BuildTiccmd (g_game.c).
void speedrun_apply_touch_controls(int *forward, int *side, short *angleturn)
{
    if (speedrun_move_dy)
        // dy is screen-space: pushing the stick UP is a *negative* dy,
        // but forward motion needs a *positive* contribution.
        *forward -= speedrun_move_dy * 50 / 100;  // +-100 -> +-50 == forwardmove[1]
    if (speedrun_move_dx)
        *side += speedrun_move_dx * 24 / 100;     // +-100 -> +-24 == 60% of sidemove[1]
    if (speedrun_turn_dx)
        // Pushing right (positive dx) turns right, which *decreases*
        // angleturn -- matches the keyboard/mouse turn code above the
        // call site.
        *angleturn -= (short)(speedrun_turn_dx * 768 / 100); // +-100 -> +-768 == 60% of angleturn[1]

    // Stroller bookkeeping (see speedrun_run_tics above), taken after the
    // touch input has been folded in so a full stick push counts as a run
    // exactly like the run key does.
    if (*forward > 25 || *forward < -25)
        speedrun_run_tics++;
    if (*side != 0)
        speedrun_strafe_tics++;
}
