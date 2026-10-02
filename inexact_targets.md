# Skipping path searches is not exact ("inexact targets")

Status: investigated 2026-09-30, **dropped for now**. Nothing in this mod skips path searches any more.

## The idea

Many AI functions loop over candidate targets (plots, cities) and run `generatePath(target, 0, true, ...)`
for each one. The final value of a target is `baseValue * 1000 / (pathTurns + k)`, so it can never be higher
than with a path of 0 turns. If that upper bound cannot beat the best value found so far, the target cannot
win, and its path search (and other checks) can be skipped. The arithmetic is exact; the result of the function
should be the same, only faster.

## Why it is not exact

The searches use `bReuse = true`. The exe's A* (`FAStar::GeneratePath`, not part of the SDK, so this is
inferred from behavior, not read from its code) seems to keep its lists between searches from the same start
and answer later targets from nodes an earlier search already expanded (`AI_pillage`: ~8 us per search, most
of them answered almost for free). The result of a search can therefore depend on which searches ran before it.
Removing searches from such a sequence changes what later searches return, even though the skipped ones could
never have been chosen.

## Evidence

### 1. The `AI_pillage` / `AI_pillageRange` skip (never committed)
Large map (turn 600, 286 cities, ~3400 units), round before turn 625:

| | Without skip | With skip |
|---|---|---|
| Path searches (`generatePath` calls) | 251,579 | 31,368 |
| Searches from `AI_pillage` | 215,512 | 8 |
| `AI_pillage` total time | 2.84 s | 0.69 s |
| DLL time (profiler scopes on in both) | 14.8 s | 12.7 s |
| Round wall clock | 16.0 s | 13.9 s |

The game was identical at the sampled turns of the large map (turns 600, 624, 625), but the 500-turn
reference scenario (`FIXED_RANDOM_SEED` 12345) **diverged at turn 390** (random seed differs, 594 instead of
593 units). The same build with the skip switched off (`AI_PILLAGE_SKIP` 0) was identical to the reference for
all 500 turns, so the crash fix, the profiler scopes and the other changes are exact; the skip is the cause.

### 2. `AI_spreadReligion` and `AI_nextCityToImprove` skips (committed in `2fdecff`)
Same kind of shortcut (`AI_spreadReligion`: path-independent part of the value moved before the search;
`AI_nextCityToImprove`: upper bound `iValue * 1000`, x2 for the capital, before `AI_bestCityBuild` and the
final `generatePath`). Their A/B tests passed (300 turns against the pre-skip baseline in `2fdecff`; 500 turns
with a runtime switch off, and 26 large-map rounds with the switch on and off: identical on every field, wall
clock 327.4 s vs 330.1 s, i.e. no measurable gain). They rely on the same search sequences, so the tests show
only that it did not matter in those runs, not that it cannot. Both were removed.

### 3. Reuse check (`PATH_REUSE_CHECK`, Timing build; removed again after this measurement)
A temporary diagnostic in `CvSelectionGroup::generatePath` repeated every `bReuse` search without reuse and
compared success, turns, cost (g), path length, end-of-turn plot and first plot (logged to
`Logs\PathReuseCheck.log`). The game then followed the non-reuse result, so such a run was a different game.
500-turn scenario, 980,000 searches, running summary at turn 495:

| Difference between reuse and fresh search | Count |
|---|---|
| Any | 41,756 (4.3%) |
| Success (path found or not) | 0 |
| Number of turns | 6 (reuse slower in all 6) |
| Cost (g) | 8,635 (0.9%) |
| Path length | 337 |
| Plot where the turn ends | 21,203 |
| First plot of the path | 22,623 |

Only the first 2000 differences were logged in detail (game turns 0-81): 1829 of them have the same turns, cost
and length and differ only in the end-of-turn or first plot (equal paths, different tie-break); in the 167
cost differences the reuse path was always the cheaper one, by about 1% with the same turns. The six cases
where reuse needed more turns were not in the logged part and were not inspected.

So reuse-dependence is real but mostly harmless: a tie-break between equally good paths, rarely a slightly
different cost, very rarely one turn more. The game after a different tie-break is still a valid game, but it
is a different game, so it cannot be verified with the "identical `AutoPlay.log`" rule.

## What was changed

- `AI_spreadReligion` and `AI_nextCityToImprove` (`CvUnitAI.cpp`) restored to the original SDK text
  (verified identical to `708bd95`). The `AI_upgrade` changes from `2fdecff` (no path searches) stay.
- The pillage skip (`AI_pillageCannotBeat`, `AI_isBaseBonusValueCached`, `AI_PILLAGE_SKIP`) was removed before
  it was ever committed.
- The reference `Reference_500turns` was produced with the religion and city skips active. A 500-turn run with
  them switched off was identical to it, so the reference is still valid for this scenario.
- Kept: the path-search caches (`pathValid` / `pathCost` per-search caches), which do not change which searches
  run.
- The reuse check was removed again (`CvSelectionGroup.cpp`, the `PATH_REUSE_CHECK` define). To repeat the
  measurement it has to be rewritten: call `GeneratePath` a second time with `bReuse = false` right after each
  `bReuse` search and compare the two results as described above.

## If this is picked up again

- Changes that only make a search cheaper are exact. Changes that remove or reorder searches with `bReuse` are
  not, however correct the bound is.
- Accepting them would need a different acceptance test than the identical log: e.g. many seeds/scenarios and a
  comparison of outcome statistics (cities, techs, score per turn), or a mode that makes each search independent
  (`bReuse = false` everywhere: exact by construction for the reordering, but every search becomes more
  expensive). The reuse check shows how rarely the results differ.
- Expected gain of the pillage skip alone: ~2 s per round (~13%) on the large map late game.
