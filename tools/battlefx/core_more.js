/* Battle FX: EmeraldTogether's additions to the package's core (tools/battlefx/core_more.js), appended to
   battlefx/bfx_core.js by tools/battlefx/extend_folder.py after the package's bundle, so they load with it (the game
   reads one script). They only add: callbacks, tasks and HD presentation for the moves past the package's 621 that
   tools/battlefx/convert_more.py converts; nothing the package's own moves use is replaced.

   Callbacks and tasks are re-implemented from this game's C (src/battle_anim_new.c, battle_anim_flying.c), statement
   for statement, as the package's behaviour files are (the parity tests compare every frame of the game's effect
   sprites with them). */
BFX_RT.define(function (A) {
'use strict';
const { CB, TK } = A;
const args = () => A.gBattleAnimArgs;

// ---------------------------------------------------------------- Stone Axe (gSpriteTemplate_StoneAxeSlash)
// SpriteCB_HorizontalSlice: arg 0, 1: initial x / y offset, arg 2: slice distance, arg 3: speed, arg 4: 1 = to the left
CB.SpriteCB_HorizontalSlice = function (s) {
  const a = args();
  s.x2 = a[0]; s.y2 = a[1];
  s.data[0] = a[2]; s.data[1] = a[3]; s.data[2] = a[4]; s.data[3] = 0;
  s.callback = horizontalSliceStep;
};
function horizontalSliceStep(s) {
  if (s.data[2] === 1) s.x2 -= s.data[1]; else s.x2 += s.data[1];
  s.data[3] += s.data[1];
  if (s.data[3] >= s.data[0]) A.destroySpriteAndMatrix(s);
}

// ---------------------------------------------------------------- the Forces of Nature's storms (battle_anim_flying.c)
// AnimEllipticalGustCentered: AnimEllipticalGust centred between the targets in a double battle
CB.AnimEllipticalGustCentered = function (s) {
  if (A.isDoubleBattle()) A.initSpritePosToAnimTargetsCentre(s, false); else A.initSpritePosToAnimTarget(s, false);
  s.y += 20;
  s.data[1] = 191;
  s.callback = ellipticalGustStep;
  s.callback(s);
};
function ellipticalGustStep(s) {   // AnimEllipticalGust_Step
  s.x2 = A.Sin(s.data[1], 32);
  s.y2 = A.Cos(s.data[1], 8);
  s.data[1] += 5;
  s.data[1] &= 0xFF;
  if (++s.data[0] === 71) A.destroyAnimSprite(s);
}

// ---------------------------------------------------------------- battle_anim_new.c's helpers (static there)
function loadBattleAnimTarget(arg) {   // LoadBattleAnimTarget
  const v = A.gBattleAnimArgs[arg];
  if (A.isDoubleBattle()) {
    if (v === 0) return A.gBattleAnimAttacker;                                  // ANIM_ATTACKER
    if (v === 2) return A.getPartnerBattler(A.gBattleAnimAttacker);             // ANIM_ATK_PARTNER
    if (v === 3) return A.getPartnerBattler(A.gBattleAnimTarget);               // ANIM_DEF_PARTNER
    return A.gBattleAnimTarget;
  }
  return v === 0 ? A.gBattleAnimAttacker : A.gBattleAnimTarget;
}
function initSpritePosToGivenTarget(s, target) {   // InitSpritePosToGivenTarget
  s.x = A.getBattlerSpriteCoord2(target, A.C.BATTLER_COORD_X);
  s.y = A.getBattlerSpriteCoord2(target, A.C.BATTLER_COORD_Y);
  A.setAnimSpriteInitialXOffset(s, A.gBattleAnimArgs[0]);
  s.y2 = A.gBattleAnimArgs[1];
}

// ---------------------------------------------------------------- Prismatic Laser, Grav Apple, Take Heart
// SpriteCB_FallingObject: arg 0, 1: x offset, height; arg 2: speed; arg 3: anim battler
CB.SpriteCB_FallingObject = function (s) {
  const a = args(); const target = loadBattleAnimTarget(3);
  if (!A.isBattlerSpriteVisible(target)) { A.destroyAnimSprite(s); return; }
  s.x = A.getBattlerSpriteCoord2(target, A.C.BATTLER_COORD_X);
  s.y = A.getBattlerSpriteCoord2(target, A.C.BATTLER_COORD_Y);
  s.x2 = a[0];
  s.y = a[1];
  s.y2 = -a[1];
  if (A.isOnPlayerSide(A.gBattleAnimTarget)) { s.y += 45; s.y2 -= 45; }
  s.data[3] = a[2];
  s.callback = fallingObjectStep;
};
function fallingObjectStep(s) {
  switch (s.data[0]) {
    case 0:
      s.y2 += s.data[3];
      if (s.y2 >= 0) { s.y2 = 0; s.data[0]++; }
      break;
    case 1:
      if (++s.data[1] > 0) {
        s.data[1] = 0;
        s.invisible = !s.invisible;
        if (++s.data[2] === 10) A.destroySpriteAndMatrix(s);
      }
      break;
  }
}

// ---------------------------------------------------------------- Photon Geyser, Baddy Bad
// SpriteCB_PhotonGeyserBeam: arg 0, 1: offset; arg 2: anim battler; arg 3: duration; arg 4: anim; arg 5: affine delay
CB.SpriteCB_PhotonGeyserBeam = function (s) {
  const a = args(); const target = loadBattleAnimTarget(2);
  if (!A.isBattlerSpriteVisible(target)) { A.destroyAnimSprite(s); return; }
  initSpritePosToGivenTarget(s, target);
  A.startSpriteAnim(s, a[4]);
  s.data[0] = a[3];
  s.data[1] = a[5];
  s.callback = beamUpStep;
};
function beamUpStep(s) {   // SpriteCB_BeamUpStep
  if (s.data[1]-- === 0) A.startSpriteAffineAnim(s, 1);
  if (s.data[0]-- <= 0) A.destroySpriteAndMatrix(s);
}

// ---------------------------------------------------------------- Buzzy Buzz, Spirit Break
CB.SpriteCB_SpriteOnMonUntilAffineAnimEnds = function (s) {
  const target = loadBattleAnimTarget(0);
  if (!A.isBattlerSpriteVisible(target)) { A.destroyAnimSprite(s); return; }
  s.x = A.getBattlerSpriteCoord(target, A.C.BATTLER_COORD_X_2);
  s.y = A.getBattlerSpriteCoord(target, A.C.BATTLER_COORD_Y_PIC_OFFSET);
  A.storeSpriteCallbackInData6(s, A.destroySpriteAndMatrix);
  s.callback = A.runStoredCallbackWhenAffineAnimEnds;
};

// ---------------------------------------------------------------- the battlers hidden and shown (battle_anim_new.c)
// Chloroblast, Ceaseless Edge, Lunar Blessing, Revival Blessing
TK.AnimTask_AllBattlersInvisible = function (id) {
  for (let i = 0; i < A.gBattlersCount; i++) {
    const sid = A.gBattlerSpriteIds(i);
    if (sid !== 0xFF && A.gSprites[sid]) A.gSprites[sid].invisible = true;
  }
  A.destroyAnimVisualTask(id);
};
TK.AnimTask_AllBattlersVisible = function (id) {
  for (let i = 0; i < A.gBattlersCount; i++) {
    const sid = A.gBattlerSpriteIds(i);
    if (A.isBattlerSpriteVisible(i) && sid !== 0xFF && A.gSprites[sid]) A.gSprites[sid].invisible = false;
  }
  A.destroyAnimVisualTask(id);
};
TK.AnimTask_AllBattlersInvisibleExceptAttackerAndTarget = function (id) {
  for (let i = 0; i < A.gBattlersCount; i++) {
    const sid = A.gBattlerSpriteIds(i);
    if (sid === A.getAnimBattlerSpriteId(0) || sid === A.getAnimBattlerSpriteId(1)) continue;   // ANIM_ATTACKER, ANIM_TARGET
    if ((sid !== 0xFF || !A.isBattlerSpriteVisible(i)) && A.gSprites[sid]) A.gSprites[sid].invisible = true;
  }
  A.destroyAnimVisualTask(id);
};
});

/* ---------------------------------------------------------------- HD presentation of the moves past the package
   The HD layer draws every effect sprite with the kit of its graphics tag / template (the package's), as for the
   package's moves; these configs add each move's identity the way the package's own configs do (src/hd/v3*.js), from
   the package's effects only, keyed to the script's own sprites and tasks (`on`) so they follow the game's timing.
   Presentation only: nothing here changes a frame of the replay (the parity tests compare the game's sprites). */
(function (root) {
'use strict';
const HD = root.BFX_HD; if (!HD || !HD.moves) return;
const FPS = HD.FPS;
const on = (name, kind, o, nth) => Object.assign({ on: name, nth: nth == null ? 1 : nth, kind }, o || {});
const at = (f, kind, o) => Object.assign({ at: f, kind }, o || {});
const IS = HD.IMPACT_STYLES || {};

// a terrain's field of psychic motes (as Misty Terrain's e_field 'misty', in Psychic Terrain's pink)
if (HD.MOVE_FX && HD.FX && HD.FX_STEP && HD.PS && HD.PS.e_sparkle) {
  HD.MOVE_FX.et_psyfield = (d, e) => d.addFx('et_psyfield', { life: (e.dur || 150) / FPS });
  HD.FX.et_psyfield = function () {};
  HD.FX_STEP.et_psyfield = function (d, f, age) {
    if (age / f.life > 0.9) return; const r = d.rng;
    if (d.frame % 3 === 0) d.emit(HD.PS.e_sparkle, [r.range(-10, 12), r.range(0.2, 1.4), r.range(-13, 5)], 1, { ramp: 'psychic', scale: 1.25 });
  };
}

HD.moves({
  // Bug: the startle (as Astonish), the lunge and the pound
  MOVE_FIRST_IMPRESSION: { impactStyle: IS.TYPE_BUG, fx: [
    at(2, 'b_glow', { b: 'attacker', dur: 30, color: [0.55, 1, 0.3], amt: 0.4, rate: 7, ramp: 'bug', rings: false }),
    on('gFirstImpressionPoundTemplate', 'c_burst', { b: 'target', style: 'bug', size: 0.85 }),
    on('AnimTask_ShakeMonInPlace', 'camera', { focus: 'target', dur: 34, zoom: 0.12 })] },
  // Ground: Mega Kick's strike with a horseshoe, dust where it lands
  MOVE_HIGH_HORSEPOWER: { tpl: { gHighHorsepowerHorseshoeTemplate: { kit: 'strike', cell: 'foot', a_throw: true, a_heavy: 1.3, a_el: 'fight' } }, fx: [
    at(2, 'a_glowcap', { b: 'target', dur: 150, max: 0.05 }),
    on('AnimTask_ShakeMon2', 'a_themecap', { hold: 10, fade: 16 }),
    on('AnimTask_ShakeMon2', 'd2_thud', { b: 'target', size: 0.8, n: 4 }),
    on('AnimTask_ShakeMon2', 'camera', { focus: 'target', dur: 40, zoom: 0.14 })] },
  // Grass: the target's strength drawn out in red, the user healed
  MOVE_STRENGTH_SAP: { fx: [
    at(3, 'c_glow', { b: 'target', dur: 36, max: 0.18 }),
    on('gStrengthSapAbsorbTemplate', 'drain', { from: 'target', to: 'attacker', dur: 64, ramp: 'red', rate: 24 }),
    on('gGrantingStarsSpriteTemplate', 'aura', { b: 'attacker', dur: 50, color: [0.5, 1, 0.4], ramp: 'heal', size: 0.9, distort: false })] },
  // Normal: Encore's stage light (the GBA spotlight is an OBJ-window mask the HD layer does not draw)
  MOVE_SPOTLIGHT: { fx: [
    on('AnimTask_CreateSpotlight', 'a_stage', { dur: 165, dark: 0.5 }),
    on('AnimTask_CreateSpotlight', 'a_spot', { b: 'target', dur: 165 })] },
  // Dark: Karate Chop's hand, a dark hit
  MOVE_THROAT_CHOP: { impactStyle: IS.TYPE_DARK, tpl: { gKarateChopSpriteTemplate: { kit: 'strike', cell: 'hand', a_path: 'chopLow', a_el: 'dark', a_heavy: 0.7, v3size: 0.55 } }, fx: [
    on('AnimTask_ShakeMon', 'c_burst', { b: 'target', style: 'dark', size: 0.75 }),
    on('AnimTask_ShakeMon', 'camera', { focus: 'target', dur: 30, zoom: 0.1 })] },
  // Psychic: the terrain's field (as the package's terrains)
  MOVE_PSYCHIC_TERRAIN: { fx: [
    on('AnimTask_BlendMonInAndOut', 'aura', { b: 'attacker', dur: 40, color: [1, 0.45, 0.85], ramp: 'psychic', size: 0.9, distort: false }),
    on('gPsychicTerrainOrbsTemplate', 'e_env', { theme: 'psychic', amt: 0.26, dur: 140 }),
    on('gPsychicTerrainOrbsTemplate', 'et_psyfield', { dur: 140 })] },
  // Bug: the green charge on the user, the lunge's hit
  MOVE_LUNGE: { impactStyle: IS.TYPE_BUG, fx: [
    at(3, 'b_glow', { b: 'attacker', dur: 230, color: [0.45, 1, 0.4], amt: 0.45, rate: 8, ramp: 'bug', rings: false }),
    on('gLungeGreenImpactTemplate', 'c_burst', { b: 'target', style: 'bug', size: 0.95 }),
    on('AnimTask_ShakeMon', 'camera', { focus: 'target', dur: 34, zoom: 0.12 }, 2)] },
  // Dark: the user powered up, each impact dark
  MOVE_POWER_TRIP: { impactStyle: IS.TYPE_DARK, fx: [
    on('AnimTask_BlendColorCycle', 'aura', { b: 'attacker', dur: 70, color: [0.62, 0.36, 1], ramp: 'dark', size: 1, distort: false }),
    on('gPowerTripImpactTemplate', 'c_burst', { b: 'target', style: 'dark', size: 0.6 }, 1),
    on('gPowerTripImpactTemplate', 'c_burst', { b: 'target', style: 'dark', size: 0.6 }, 2),
    on('gPowerTripImpactTemplate', 'c_burst', { b: 'target', style: 'dark', size: 0.7 }, 3)] },
  // Fire: the user burns red-hot (as Bide's glow), the flames engulf the target
  MOVE_BURN_UP: { impactStyle: IS.TYPE_FIRE, fx: [
    on('AnimTask_ShakeMon', 'b_glow', { b: 'attacker', dur: 70, color: [1, 0.3, 0.08], amt: 0.75, rate: 7, ramp: 'fire', motes: 1, flash: 0.25 }),
    on('gFireSpreadSpriteTemplate', 'c_engulf', { b: 'target', dur: 44, ramp: 'fire', size: 0.95, height: 1.05 }),
    on('gFireSpreadSpriteTemplate', 'c_burst', { b: 'target', style: 'fire', size: 1 }),
    on('gFireSpreadSpriteTemplate', 'camera', { focus: 'target', dur: 40, zoom: 0.12 })] },
  // Psychic: both battlers glow as their speed changes hands
  MOVE_SPEED_SWAP: { fx: [
    on('gSpeedSwapCircleTemplate', 'aura', { b: 'target', dur: 40, color: [1, 0.5, 0.88], ramp: 'psychic', size: 0.85, distort: false }),
    on('gSpeedSwapOrbMissileTemplate', 'drain', { from: 'target', to: 'attacker', dur: 30, ramp: 'psychic', rate: 26 }),
    on('gSpeedSwapOrbMissileTemplate', 'aura', { b: 'attacker', dur: 40, color: [1, 0.5, 0.88], ramp: 'psychic', size: 0.85, distort: false })] },
  // Dragon: the hammer's slam
  MOVE_DRAGON_HAMMER: { impactStyle: IS.TYPE_DRAGON, fx: [
    on('AnimTask_SquishTarget', 'g2_slam', { b: 'target', strength: 1.2, crack: true, chips: 4, shake: 0.5, hit: false }),
    on('AnimTask_SquishTarget', 'c_burst', { b: 'target', style: 'dragon', size: 1 }),
    on('AnimTask_SquishTarget', 'camera', { focus: 'target', dur: 40, zoom: 0.14 })] },
  // Psychic: Crunch's jaws, a psychic flare where they close
  MOVE_PSYCHIC_FANGS: { tpl: { gSharpTeethSpriteTemplate: { kit: 'b2_bite', style: 'normal', scale: 1.15 } }, fx: [
    on('AnimTask_ShakeMon', 'c2_flare', { b: 'target', dur: 26, color: [1, 0.4, 0.85], size: 0.9, flare: 0.3 })] },
  // Water: Waterfall's cloak and cascade
  MOVE_LIQUIDATION: { impactStyle: IS.TYPE_WATER, fx: [
    at(1, 'b_cloak', { el: 'water', lead: 10 }),
    on('AnimTask_ShakeMon2', 'g2_cascade', { b: 'target', dur: 40 })] },
  // Normal: Baby-Doll Eyes' gaze in tearful blue, the target moved
  MOVE_TEARFUL_LOOK: { tag: { ANIM_TAG_OPENING_EYE: { kit: 'none' } }, fx: [
    at(14, 'h2_glare', { color: [0.55, 0.78, 1], flash: 22, n: 1, delay: 40, travel: 10, cone: 0, react: null, fear: false }),
    on('gTearDropSpriteTemplate', 'h2_react', { b: 'target', react: 'sag', dur: 30, amt: 0.8 })] },
  // Rock: the axe's cut, stone chips and dust
  MOVE_STONE_AXE: { impactStyle: IS.TYPE_ROCK, fx: [
    on('AnimTask_ShakeMon', 'd2_thud', { b: 'target', size: 0.7, n: 4 }),
    on('AnimTask_ShakeMon', 'c_burst', { b: 'target', style: 'ground', size: 0.7 }),
    on('AnimTask_ShakeMon', 'camera', { focus: 'target', dur: 30, zoom: 0.1 })] },
  // Dark: the bow, the glare, the great cross cut
  MOVE_KOWTOW_CLEAVE: { impactStyle: IS.TYPE_DARK, fx: [
    on('gSpriteTemplate_LargeCrossImpact', 'c_slashx', { b: 'target', color: [0.75, 0.45, 1] }),
    on('gSpriteTemplate_LargeCrossImpact', 'c_burst', { b: 'target', style: 'dark', size: 1 }),
    on('gSpriteTemplate_LargeCrossImpact', 'camera', { focus: 'target', dur: 40, zoom: 0.15 }),
    on('gSpriteTemplate_LargeCrossImpact', 'grade', { dur: 60, sat: 0.5, tint: [0.8, 0.6, 1], amt: 0.25 })] }
});
})(typeof window !== 'undefined' ? window : globalThis);
