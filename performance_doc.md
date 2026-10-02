# CIV4_performance_booster: performance changes

Base: original BtS 3.19 SDK (`708bd95`). All performance changes are marked in the code with a
`Performance:` comment; auto-play tooling is marked with `AI takeover` / `CIV4_performance_booster`.

**Rule:** a performance change is only accepted if it does not change the game. Each one was verified
with a fixed-seed A/B auto-play run: loaded same savegame in both saves, ran for 500 turns and compared results.

Result of all changes on the 500-turn reference run: 194.8 s wall clock, 141.4 s in the DLL
(commit `18fff19`, which still contained the religion and city target skips; they were removed later, see
`inexact_targets.md`; the game is identical, the time may be slightly different). Per-change figures below are
from the commit messages (different runs, so they do not add up exactly).

## Switches (`Assets/XML/GlobalDefinesAlt.xml`)

| Define | Default | Meaning |
|---|---|---|
| `AI_FRAME_TIME_BUDGET_MS` | 2000 | Time budget for extra game-update frames while only the AI moves. 0 = one per exe frame (BtS). |
| `MAP_SYMBOL_BATCHING` | 1 | Redraw road/yield symbols once per batch instead of on every change during AI turns. 0 = off. |
| `PYTHON_SKIP_TRIVIAL_CALLBACKS` | 1 | Do not call Python callbacks that only return a constant. 0 = always call. |
| `AUTOPLAY_SKIP_AUTOSAVE` | 1 | No autosaves while AI auto-play runs. |
| `FIXED_RANDOM_SEED` | 12345 | Testing aid: every new game/scenario uses the same random numbers (0 = off). **Set to 0 for normal play.** |
| `AUTOPLAY_RAND_LOG_TURN` | 0 | Turn whose random draws are logged to `AutoPlayRand.log` (0 = off). |
| `PERF_MEMORY_LOG` | 0 | 1 = per-turn `Logs\PerfMemory.log`. |

## Game-time optimizations (do not change the game)

### Extra frames during AI turns (`CvGame::update`, `updateFrame`)
While only the AI moves (single player, no human turn, no diplomacy screen), several frames of game logic
run per exe frame, up to `AI_FRAME_TIME_BUDGET_MS`. Each extra frame is the complete per-frame logic
(`updateFrame`, the body of the original `update`), so the game goes through the same steps as with one
frame per exe frame; only the redrawing in between is skipped. Budget 500 -> 2000 ms: 258.2 s -> 210.2 s
wall clock.

### Path searches (`CvGameCoreUtils.cpp`)
`pathDestValid` runs once at the start of every search and starts a new cache generation (stamps are only
compared for equality, so wrap-around is safe). Caches are only used for the group that started the search.
- **Plot danger** (`AI_getPlotDanger`) cached per search.
- **Group checks and per-plot move checks** in `pathValid`: `canMoveThrough` / `canMoveOrAttackInto`
  (a loop over all units of the group), and for civilian groups `canFight` / `alwaysInvisible`.
  `pathValid` 14.5 s -> 8.4 s.
- **Step movement cost** in `pathCost` (single-unit groups) and the **defense modifier** of a plot for the
  group's team. `generatePath` ~24 s -> 17.2 s together with the `pathValid` caches; DLL 84.4 s -> 78.6 s.
- **Equivalent units in multi-unit groups** in `pathCost` (`s_aiPathUnitClass`): pathCost evaluates every unit
  of the group and keeps the worst result. Units that pathCost cannot tell apart (same unit type, owner, base
  moves, move discount, base combat strength, river, enemy route, and double move flags for hills, every
  terrain and every feature: everything `pathCost`, `CvPlot::movementCost`, `isValidRoute`, `CvUnit::isEnemy`
  and `isVisibleEnemyDefender` read from a unit, `getPathUnitSignature`) give identical results, so only the
  first unit of each such class is evaluated. Exact: the worst value only ever gets lower, so a repeated
  identical unit never passes the update tests. The classes are calculated once per search for the group that
  started it. On the large map 65% of the unit evaluations were in multi-unit groups (groups of 11+ units
  average ~40 units). Result (large map, turns 600-626, identical random seeds): 324M of the 511M multi-unit
  evaluations skipped (63%); 325.8 s -> 293.8 s wall clock (12.53 -> 11.30 s per round). On the 500-turn
  scenario half of the multi-unit evaluations are skipped, but few groups are large there (no visible gain).
- **Per-search unit and group values** (`PathUnitInfo`, `updatePathGroupFlags`): `pathCost` no longer looks up
  the units of the group and asks each one for its moves, combat ability, team, river and defensive bonus on
  every call: these values are calculated once per search for the units that are evaluated (the first of each
  class) and kept in `s_aPathUnits`; the per-unit body is `pathCostUnit`. `pathValid` (and `pathCost`) read the
  group's domain, plot, head team, head owner and `AI_isControlled()` from per-search variables instead of
  looking up the group's head unit on every call. Groups other than the one that started the search (none
  seen) use the old per-call calculation. Nothing changes during a search, so the results are the same.
- **`pathAdd`** (every node except the initial add): for the group that started the search it uses the same
  per-search values: only the first unit of each class (the result is the minimum over the units, and equivalent
  units give the same value), `iMaxMoves` from `PathUnitInfo`, and for a single unit the cached step cost
  (`getPathStepCost`, the value `pathCost` just used for the same step) instead of `movementCost` for every unit.
  The initial add keeps the old code (it may come before `pathDestValid` sets up the search).
- **Search flags** in `pathValid`: `GetInfo(finder)` (a call into the exe) is read once per call instead of up
  to four times; the flags do not change during a search. Not measurable on the large map (~0.5%).

### Skipping path searches is NOT done (see `inexact_targets.md`)
"Skip hopeless targets before the path search" shortcuts are not exact: path searches with `bReuse` depend on
which searches ran before them. `AI_spreadReligion`, `AI_nextCityToImprove` (both were in `2fdecff`) and the
`AI_pillage` skip (never committed) were removed or not added; both functions are identical to the original
SDK again. Details and numbers: `inexact_targets.md`.

### Unit upgrades (`CvUnitAI::AI_upgrade`)
Only taken when the owner's strategies are already cached for this turn (`AI_isStrategyHashCached`).
`AI_unitValue` asks for the strategies and computes the hash on the first request of a turn, so skipping
that first request computed them later from a different game state and changed an AI war decision at turn 63
(fixed in `64cdba8`). With cached strategies:
- Early exit if `isReadyForUpgrade()` is false or the unit has no upgrade class.
- Only the unit types this unit could ever upgrade to are visited (`getUpgradeTargets`, calculated once per
  civilization and unit type from the XML, same order).
- The team's techs are checked (`teamHasUnitTechs`) before `canUpgrade()`, which starts with the upgrade
  price (a Python call) and a search for a city that can train the unit. Not done when the canTrain Python
  callback is on.
- The unit's own value is calculated only when first needed; the random number is drawn under the same
  conditions as before. `AI_upgrade` 4.4 s -> 2.8 s.

### Citizen evaluation cache (`CvCityAI`)
`AI_beginCitizenEval()` / `AI_endCitizenEval()` (nestable) mark a window in which the caller only evaluates
(`AI_plotValue`, `AI_specialistValue`) and does not change the city. Inside it, `AI_yieldValue` uses values
that depend only on the city state, calculated once per window (not saved):
- food difference, stored food, growth threshold, health, happiness, extra free specialists, working
  population, food rate and hurry cost modifier (`AI_getCitizenEvalFoodData`);
- number of good unworked tiles and good specialists (rates all 21 city plots otherwise).
Used by `AI_addBestCitizen`, `AI_removeWorstCitizen`, `AI_juggleCitizens` (worst-plot loop),
`AI_updateBestBuild` (yield valuation block) and the two read-only loops of `AI_updateWorkersNeededHere`.
`AI_yieldValue` 13.2 s -> 9.1 s (good tiles/specialists) -> 5.5 s (food values); worker planning together
with the removed profiler scopes: DLL 78.6 s -> 71.1 s; juggling: `AI_yieldValue` 10.9 s -> 9.5 s (500-turn run).

### `CvPlayerAI::AI_getPlotDanger` (no cache)
The function loops over the (2 * range + 1)^2 plots around the plot (81 for the default range 4) and was 9% of the
DLL time on the large map (9.6M calls per 26 rounds). Same result, less work per plot: the team and coordinates
are read once; the border danger (only added for AI players and non-city plots) is only evaluated at distance 1
or 2; plots without units that cannot be border plots are skipped before the area lookup; the distance is
`max(|dx|, |dy|)` when the range is less than half the map in both directions (then the wrapping in
`stepDistance` cannot shorten it), else `stepDistance`. A cache across searches was looked at
and dropped: `canMoveInto` reads too much state (fortify state of defenders, mission AI of the group, force
peace, permanent war/peace, area border obstacles, the Python callback, ...) to cover with a list of
invalidating changes.

### Smaller algorithmic changes
- `CvPlayerAI::AI_baseBonusVal`: skips `canConstruct()` for buildings that do not use the bonus (their value
  is 0 and the following code only multiplies, divides or zeroes it). 5.6 s -> 1.0 s.
- `CvTeamAI::AI_techTrade`: the world unit and world wonder classes requiring each tech are built once from
  the XML (`initTechWorldClasses`) instead of looping over all unit and building types. 5.0 s -> 3.8 s.
- `CvPlayer::getBestRoute`: only visits build types that build a route (same order) and skips the read-only
  `canBuild` check for routes that cannot beat the best one. `AI_updateRouteToCity` 4.0 s -> 2.9 s.
- `CvUnit::updatePlunder`: only rebuilds the plot groups of teams whose trade network
  changed. Trade routes are still recalculated for every player in the same order as
  `CvGame::updatePlotGroups()` did.

### Python callbacks (`PerfPythonCallbacks.cpp`, `CvAppInterface.py`)
The DLL calls many `CvGameUtils` callbacks for every unit update, city turn etc.; in unmodified BtS most
only return False. `perfConstantCallback` (Python) inspects the bytecode once per session and callback: the
`CvGameInterface` function must be exactly `return gameUtils().<name>(argsList)` and the `CvGameUtils`
method may only unpack its arguments and return a constant (no calls, no attribute access, except
True/False/None). The DLL then uses the constant (`perfConstantPythonCallback`). Callbacks replaced by a mod
are called as before. Used for 16 callbacks (`AI_unitUpdate`, `AI_doWar`, `AI_doDiplo`, `AI_chooseTech`,
`doGrowth`, `doCulture`, `doPlotCulture`, `doProduction`, `doReligion`, `doGreatPeople`, `doMeltdown`,
`doCombat`, `getUpgradePriceOverride`, ...). The outcome per callback is written to
`Logs\PerfPythonCallbacks.log`. Together with `AUTOPLAY_SKIP_AUTOSAVE`: wall clock 210.2 s -> 194.8 s, DLL
154.3 s -> 141.4 s.

## Graphics-only optimization

### Map symbol batching (`CvPlot.cpp`, `MAP_SYMBOL_BATCHING`)
While no human player has an active turn (AI turns, AI auto-play), `updateRouteSymbol`, `updateSymbols` and
`updateSymbolDisplay` only mark the plot (`markMapSymbolsDirty`); each marked plot is redrawn once by
`CvPlot::flushMapSymbols()` at the start of the next game turn, when a human turn starts or when auto-play
ends. The game never reads these symbols. Not used in network multiplayer. Symbol updates ~3.5 s -> ~2.3 s.

## Auto-play tooling (changes behavior only during AI auto-play)
- **AI takeover** (`CvGame::setAIAutoPlay`, `CvPlayer::setHumanDisabled`): auto-play no longer kills the
  active player's units and cities. While `setHumanDisabled` is set `isHuman()` is false (saved with the
  player, save flag 2); the AI plays the civilization and hands it back afterwards. No camera jump when the
  AI founds a city for the active player. Use: `CheatCode = chipotle`, Python console
  `CyGame().setAIAutoPlay(n)`.
- **Ctrl+Shift+A** (`CvEventManager.py`) opens a popup for the number of turns (or stops auto-play). No
  city naming or top-civs popups during auto-play.
- **`AutoPlay.log`**: one line per turn (random seed, map random seed, units, cities, population, gold,
  techs, turn slice) and a run summary line with the duration. Not `calculateSyncChecksum()`: its formula
  depends on the number of frames, not only on the game state.
- **`FIXED_RANDOM_SEED`** and **`AUTOPLAY_RAND_LOG_TURN`** (`CvRandom.cpp`): reproducible runs and a log of
  every game random draw of one turn, for comparing two runs.
- **`AUTOPLAY_SKIP_AUTOSAVE`**: no autosaves during auto-play; the turn that ends the run is still saved.

## Profiling (Timing build)
- Build: `CvGameCoreDLL\build_timing.bat` (= `build.bat Timing`, defines `PERF_PROFILE`, DLL is copied to
  `Assets`). Run `build.bat` afterwards for the normal Release DLL.
- `PROFILE` / `PROFILE_FUNC` scopes (`FProfiler.h`, `PerfProfiler.cpp`) use `QueryPerformanceCounter`.
  `Logs\PerfProfile.log` has one report per turn of the active player's civilization: per inter-turn in
  normal play, per full round during auto-play, plus a run summary at the end of an auto-play run.
- Extra scopes: Python callbacks `AI_chooseProduction` and `AI_unitUpdate`, event handlers (calls and time
  per `event: <name>`), steps of the game update frame, `CvGame::doTurn` (including the exe's AutoSave),
  `CvPlayer::doTurn`, `CvCityAI::AI_doTurn`, `AI_yieldValue` and parts of `AI_plotValue`.
- Path searches are listed by calling function (`path <- <caller>`, `PerfProfCallerSample` in
  `CvSelectionGroup::generatePath`) and the run summary lists all `x <- caller` samples.
- **Path finder callbacks**: `pathCost`, `pathValid` and `pathAdd` have scopes while
  `PERF_PROFILE_PATH_CALLBACKS` is 1 (source constant at the top of the path finder section in
  `CvGameCoreUtils.cpp`, default **0**, Timing build only; edit and rebuild to switch). They run inside the exe's
  `GeneratePath`, so without them their time is part of the self time of `CvSelectionGroup::generatePath()`.
  They are called for every expanded node (tens of millions of times per turn on a large map), so they distort
  the rest of the profile: set it to 1 only to split `generatePath`'s time. `pathHeuristic` is never marked.
  Level 2 also adds sections inside `pathCost` (`step cost`, `defense modifier`, `attack checks`) and
  `pathValid` (`danger check`, `can move check`; compare shares, the overhead is large: a large-map run takes
  ~55% longer). Finding on the large map (turn 625): only the step cost is a real cost, the other sections are
  mostly scope overhead.
- **Step cost counters** (`PERF_PATH_COUNTERS` = 1, source constant above `pathCost`, Timing build only, plain
  counters without timers): `Logs\PathStepCounters.log` gets a cumulative line every 2M `pathCost` calls with the
  group size distribution and how many step cost evaluations bypass the per-search cache. Large map, turns
  600-626 (378M calls, 789M unit evaluations): 74% of the calls are single-unit groups (cached), 26% are
  multi-unit groups, but those account for 65% of the unit evaluations (511M); groups of 11+ units average
  ~40 units (~1/3 of all evaluations); 48% of the multi-unit calls are groups of a single unit type.
- Scopes were removed from tiny functions called millions of times (`AI_plotValid`, `canBuild`, `canTrain`,
  `getBestRoute`, `isCoastalLand`, `calculateImprovementYieldChange`, `pathAdd`, two explore loops): their
  overhead distorted the Timing build.
- **Memory log** (`PerfMemoryLog.cpp`, `PERF_MEMORY_LOG` = 1): per turn address space,
  private/working set, committed memory by type, C runtime heap (slow with many blocks), page faults,
  file I/O, game object counts and Python object count (`perfPythonObjectCount` in `CvAppInterface.py`).
  Off it only reads the define once per turn.

## Bug fixes

### Crash when units are bumped after a war declaration (`CvPlot::verifyUnitValidPlot`)
The function collected `CvUnit*` pointers of all units on the plot and then moved them away with
`jumpToNearestValidPlot()`. Moving a unit can delete other units of the plot (a transport without a valid
plot dies with its cargo; a unit bumped onto a fogged plot with enemy units captures or bumps them, which can
chain back to this plot), which left dangling pointers and could crash, e.g. when declaring a second war. It
now keeps `IDInfo` and looks each unit up again (`::getUnit`) before use. The result is the same whenever
vanilla did not crash (verified: 500 auto-play turns identical to the reference).

## Workflow for a new optimization
1. Argue exactness (same choices, same random draws, same order of lazy caches such as the strategy hash).
2. Build Timing (`build.bat Timing`), copy the DLL to `Assets`.
3. Run the fixed-seed auto-play, diff `AutoPlay.log` against the reference, compare `PerfProfile.log`.
4. Commit only if identical; then replace the reference with the new run. `TestDLLs\` (git-ignored)
   keeps the DLL of each step for A/B runs.
