# CIV4_performance_booster: performance changes

Base: original BtS 3.19 SDK (`708bd95`). All performance changes are marked in the code with a
`Performance:` comment; auto-play tooling is marked with `AI takeover` / `CIV4_performance_booster`.

**Rule:** a performance change is only accepted if it does not change the game. Each one was verified
with a fixed-seed A/B auto-play run: scenario `performance_tester.CivBeyondSwordWBSave`,
`FIXED_RANDOM_SEED` = 12345, and the per-turn lines of `Logs\AutoPlay.log` (random seed + totals) must be
identical to the reference (ignoring the "(turn slice ...)" part). The reference is
`My Games\Beyond the Sword\Logs\Reference_500turns\` (AutoPlay.log + VabiProfile.log, 500 turns).
Civ4 clears `Logs` at startup, so copy logs away before the next run.

Result of all changes on the 500-turn reference run: 194.8 s wall clock, 141.4 s in the DLL
(commit `18fff19`). Per-change figures below are from the commit messages (different runs, so they do
not add up exactly).

## Switches (`Assets/XML/GlobalDefinesAlt.xml`)

| Define | Default | Meaning |
|---|---|---|
| `AI_FRAME_TIME_BUDGET_MS` | 2000 | Time budget for extra game-update frames while only the AI moves. 0 = one per exe frame (BtS). |
| `MAP_SYMBOL_BATCHING` | 1 | Redraw road/yield symbols once per batch instead of on every change during AI turns. 0 = off. |
| `PYTHON_SKIP_TRIVIAL_CALLBACKS` | 1 | Do not call Python callbacks that only return a constant. 0 = always call. |
| `AUTOPLAY_SKIP_AUTOSAVE` | 1 | No autosaves while AI auto-play runs. |
| `FIXED_RANDOM_SEED` | 12345 | Testing aid: every new game/scenario uses the same random numbers (0 = off). **Set to 0 for normal play.** |
| `AUTOPLAY_RAND_LOG_TURN` | 0 | Turn whose random draws are logged to `AutoPlayRand.log` (0 = off). |
| `VABI_MEMORY_LOG` | 0 | 1 = per-turn `Logs\VabiMemory.log`. |

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
- **Plot danger** (`AI_getPlotDanger`) cached per search (ported from VabiGEM).
- **Group checks and per-plot move checks** in `pathValid`: `canMoveThrough` / `canMoveOrAttackInto`
  (a loop over all units of the group), and for civilian groups `canFight` / `alwaysInvisible`.
  `pathValid` 14.5 s -> 8.4 s.
- **Step movement cost** in `pathCost` (single-unit groups) and the **defense modifier** of a plot for the
  group's team. `generatePath` ~24 s -> 17.2 s together with the `pathValid` caches; DLL 84.4 s -> 78.6 s.

### Skipping hopeless targets before path searches (`CvUnitAI.cpp`)
The value of a target can only shrink with the path length, so targets whose value with the shortest
possible path cannot beat the best one so far are skipped before the expensive path and build searches.
- `AI_spreadReligion`: the path-independent part of the value is computed first (`iBaseValue`).
- `AI_nextCityToImprove`: the value is at most `iValue * 1000` (x2 for the capital), so cities that
  cannot beat the best are skipped before `AI_bestCityBuild`.

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

### Smaller algorithmic changes
- `CvPlayerAI::AI_baseBonusVal`: skips `canConstruct()` for buildings that do not use the bonus (their value
  is 0 and the following code only multiplies, divides or zeroes it). 5.6 s -> 1.0 s.
- `CvTeamAI::AI_techTrade`: the world unit and world wonder classes requiring each tech are built once from
  the XML (`initTechWorldClasses`) instead of looping over all unit and building types. 5.0 s -> 3.8 s.
- `CvPlayer::getBestRoute`: only visits build types that build a route (same order) and skips the read-only
  `canBuild` check for routes that cannot beat the best one. `AI_updateRouteToCity` 4.0 s -> 2.9 s.
- `CvUnit::updatePlunder` (ported from VabiGEM): only rebuilds the plot groups of teams whose trade network
  changed. Trade routes are still recalculated for every player in the same order as
  `CvGame::updatePlotGroups()` did.

### Python callbacks (`VabiPythonCallbacks.cpp`, `CvAppInterface.py`)
The DLL calls many `CvGameUtils` callbacks for every unit update, city turn etc.; in unmodified BtS most
only return False. `vabiConstantCallback` (Python) inspects the bytecode once per session and callback: the
`CvGameInterface` function must be exactly `return gameUtils().<name>(argsList)` and the `CvGameUtils`
method may only unpack its arguments and return a constant (no calls, no attribute access, except
True/False/None). The DLL then uses the constant (`vabiConstantPythonCallback`). Callbacks replaced by a mod
are called as before. Used for 16 callbacks (`AI_unitUpdate`, `AI_doWar`, `AI_doDiplo`, `AI_chooseTech`,
`doGrowth`, `doCulture`, `doPlotCulture`, `doProduction`, `doReligion`, `doGreatPeople`, `doMeltdown`,
`doCombat`, `getUpgradePriceOverride`, ...). The outcome per callback is written to
`Logs\VabiPythonCallbacks.log`. Together with `AUTOPLAY_SKIP_AUTOSAVE`: wall clock 210.2 s -> 194.8 s, DLL
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
- Build: `CvGameCoreDLL\build_timing.bat` (= `build.bat Timing`, defines `VABI_PROFILE`, DLL is copied to
  `Assets`). Run `build.bat` afterwards for the normal Release DLL.
- `PROFILE` / `PROFILE_FUNC` scopes (`FProfiler.h`, `VabiProfiler.cpp`) use `QueryPerformanceCounter`.
  `Logs\VabiProfile.log` has one report per turn of the active player's civilization: per inter-turn in
  normal play, per full round during auto-play, plus a run summary at the end of an auto-play run.
- Extra scopes: Python callbacks `AI_chooseProduction` and `AI_unitUpdate`, event handlers (calls and time
  per `event: <name>`), steps of the game update frame, `CvGame::doTurn` (including the exe's AutoSave),
  `CvPlayer::doTurn`, `CvCityAI::AI_doTurn`, `AI_yieldValue` and parts of `AI_plotValue`.
- Path searches are listed by calling function (`path <- <caller>`, `VabiProfCallerSample` in
  `CvSelectionGroup::generatePath`) and the run summary lists all `x <- caller` samples.
- Scopes were removed from tiny functions called millions of times (`AI_plotValid`, `canBuild`, `canTrain`,
  `getBestRoute`, `isCoastalLand`, `calculateImprovementYieldChange`, `pathAdd`, two explore loops): their
  overhead distorted the Timing build.
- **Memory log** (`VabiMemoryLog.cpp`, ported from VabiGEM, `VABI_MEMORY_LOG` = 1): per turn address space,
  private/working set, committed memory by type, C runtime heap (slow with many blocks), page faults,
  file I/O, game object counts and Python object count (`vabiPythonObjectCount` in `CvAppInterface.py`).
  Off it only reads the define once per turn.

## Workflow for a new optimization
1. Argue exactness (same choices, same random draws, same order of lazy caches such as the strategy hash).
2. Build Timing (`build.bat Timing`), copy the DLL to `Assets`.
3. Run the fixed-seed auto-play, diff `AutoPlay.log` against the reference, compare `VabiProfile.log`.
4. Commit only if identical; then replace the reference with the new run. `TestDLLs\` (git-ignored)
   keeps the DLL of each step for A/B runs.
