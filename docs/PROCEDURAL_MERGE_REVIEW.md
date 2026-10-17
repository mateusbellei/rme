# Procedural stack integration review

Review date: 2026-10-17. Tracking: [issue #5](https://github.com/mateusbellei/rme/issues/5).

The delivery is an opt-in procedural authoring/export workflow. Physical stairs,
OTBM prefab import, generated houses/spawns/NPCs and real client/server visual QA
remain outside this delivery. Integrating these PRs does not complete issue #5.

## Findings resolved before integration

| Finding | Resolution | Evidence |
| --- | --- | --- |
| PR #4 depended on build fixes available only in PR #7 | Move the CMake/platform fixes into #4 (`736e343`) and update the dependent branches without rewriting commits | Independent complete MSVC x64 Release build; dedicated Linux Editor build workflow |
| Modern renderer used a context even when OpenGL 4.5 creation/loading failed | Refuse unsupported contexts, guard renderer switching and incomplete resource initialization (`dc4d2ed`) | Real context creation/reuse test on Windows; Linux workflow additionally requests a Mesa 3.3 driver and requires rejection |
| PNG painting ignored the recipe border opt-out | Require both the recipe setting and global automagic before cleaning/recalculating borders (`e0b8fa6`) | Regression first failed on existing border preservation, then passed through actual PNG import, categorical resize, alpha/zero/unmapped skips, undo/redo and OTBM readback |
| Replacing ordinary world tiles could destroy script Action IDs | Inspect both ground and item unique/action attributes before staging (`aa1e54c`) | Regression first reproduced the loss; grounds and items with Action IDs now reject replacement without changing tiles or undo history |

The complete updated stack passed five core/integration suites plus four
application/context tests locally in MSVC x64 Release. This includes the 300-world
settings matrix and production editor/CLI/OTBM regressions. No private DAT/SPR or
game server was used. Check each PR's current Checks before integrating; prior
successful runs do not substitute for checks of a moved head/base.

## Integration order

1. [PR #4](https://github.com/mateusbellei/rme/pull/4): optional renderer/base changes.
2. [PR #6](https://github.com/mateusbellei/rme/pull/6): PNGtoOTBM-compatible mapping.
3. [PR #7](https://github.com/mateusbellei/rme/pull/7): procedural world authoring and export.

Use merge commits to retain the development history. After each predecessor is
merged, retarget its successor to `main`, verify the resulting diff, incorporate
`main` if needed and wait for its checks. Preserve the original user's local
Data Editor/client/project changes; they are not part of this stack. Keep issue
#5 open for the remaining roadmap. No branch renaming is required.

The modern renderer remains experimental and off by default. Its visual and
performance checklist is `REDUX_MIGRATION_CHECKLIST.md`. Procedural formats,
commands, limits and known gaps are documented in `WORLD_GENERATION.md`.
