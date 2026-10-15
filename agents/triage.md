# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-15 (triage session, todo entry 10 of the sixteenth window): the `__VERSION` TS6133 and the
binary-marker TS1434 were fixed (`ttttUnusedRedeclaration`, `ttttBinaryMarkerAfter*`), the late-bound method merge
went to bugs/type_core.md and the member-symbol copies to hacks.md, /tmp's old scratch was deleted.

- **For the human** (plan 62 stage 3, 2026-10-14): tttt's `--checkers` now defaults to one per core (at most one per
  64 KB of checked source), so on the 12-core machine the corpora check with 12 checkers where they had 4. Please run
  `scripts/bench.sh --checkers "4 8 12 16"` there and paste the output into measurements.md (next to the bun table):
  the per-K wall times and RSS decide whether the default should be capped below the core count (this 4-vCPU box can't
  measure past 4; the 2026-10-11 bun session saw 12 checkers 30-50% faster than 4 at +13..40% RSS).
- **TS5088 missing** (plan 64 stage 4, 2026-10-15): tsgo reports "The inferred type of 'X' references a type with a
  cyclic structure which cannot be trivially serialized" (a declaration diagnostic, it blocks the file's .d.ts) in
  headlessui's `@headlessui-react`/`-vue` (`src/utils/disposables.ts`, `src/machine.ts`, `src/hooks/use-disposables.ts`);
  tttt writes those files instead (dtsdiff's 2 `extra file` projects). Unimplemented check: `scripts/dtsdiff.py -f
  headlessui`.
- **Content-mapped files' declaration outputs** (plan 64 stage 4): tsgo names a `.vue` file's output `x.d.vue.ts`
  (ChangeToDeclarationExtension with the content mapper's extensions) and emits a supplemental module's
  (`component.vue.0.d.ts`, with a `/// <reference path>` to it); tttt writes `x.vue.d.ts` and no supplemental one
  (`contentMapperSupplementalModule`, the suite's dts component).
- **A unique symbol member's name in TS2540** (found by plan 71's audit): `const a = Symbol(); class C { get [a]()
  { return ""; } } declare const c: C; c[a] = "x";` tsgo says `Cannot assign to '[a]'`, tttt `'__@a@62'` (the
  internal name leaks into the message; other property-name messages likely print it too).
- from human: please stop the agent loop after the next triage session
