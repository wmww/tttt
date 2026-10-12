# Triage
Uninvestigated items: dump here what you don't want to deal with now. Triage sessions drain this into `bugs/*.md`,
plans, or fixes.

Last drained 2026-10-09 (triage session, todo entry 10 of the thirteenth window): the TS 7.1 window's leftovers moved
to bugs/display.md, type_core.md, missing_checks.md, binder_modules.md, harness.md, perf_leads.md and the new
bugs/js.md (plan 26 retired into it); plans 53/54 retired, plan 58 (7.1.0 stable re-pin) written; three Opus
sub-agents worked the CLI/harness, parser and real-world checker items (the commit message has the outcome).

Finished 2026-10-11: the triage session above was cut off (usage limit) before merging its agents; the next session
merged their 7 commits and 3 uncommitted diffs (suite 13316, every oracle same or better) and filed what was left
into bugs/harness.md, sourcesweep_checker.md, js.md and display.md.

## From the bun check comparison (2026-10-11, measurements.md)
- Perf and memory vs `bun check` are now plans 59/60 (todo.md); the thread cap is in vision.md (needs a many-core
  machine). Bun's correctness numbers (conformance suite, 72-project config sweep, fuzzing, mutation of effect) are a
  bar to compare our oracles against.
- The newest-compiler build is in todo entry 10.
- npm 12 defaults to `allow-remote=none`: a corpus lock entry resolved outside registry.npmjs.org breaks
  `thirdparty/ensure-corpora.sh`. bulletproof-react's (inherited from upstream's yarn.lock, npmmirror) was rewritten;
  `--relock` may bring it back.

## From the insights of 2026-10-11
- `scripts/sourcesweep.py`'s timing lines are meaningless when other agents run (its 10-09 full run said "tttt 2801 s,
  tsc 2811 s"; idle, a 1/8 sample is 84 s vs 214 s): print the machine's load average next to them, or warn.

## From the source sweep fan-out (agent D, 2026-10-12)
- **Decision needed: tttt never checks lib files; tsc does** without skipLibCheck/skipDefaultLibCheck. Libs are clean in
  normal setups, so this only shows with partial lib sets pulled in by `/// <reference lib>` or odd `lib` lists: tsc
  then reports inside the lib. Numbers at the end of the 2026-10-12 sweeps: sourcesweep -TS2552 in
  lib.esnext.intl.d.ts in 3 projects (angularfire/test/functions, react-select/docs and react-select/.../__tests__:
  @types/node's `/// <reference lib="esnext.intl">` without es2021.intl, `DateTimeRangeFormatPart` unresolved), -TS2344
  in lib.dom.d.ts x3 in unocss/examples/vite-lit; DT sweep 1 package (react-intl-redux, the same intl case). 5 projects
  of 3363 and 1 of 8595. Checking libs fully costs on every project (tsc's own check time includes them); cheaper
  options: only the name resolution / unresolved-type diagnostics of lib files, or checking a lib file only when the
  program's lib set is incomplete (a `/// <reference lib>` set that isn't closed under the libs' own references).

## From human
- Not sure how important emitting .d.ts files are. for now emitting js should def be out of scope. there are already plenty of fast emitters (eg esbuild), so for now anything covered by them doesn't need to be a focus. otherwise up to you what's important.
- I'll test on various configurations in the future. for now I don't see why we would cap at a particular number of threads. maybe tsgo has it's reasons but cargo culting from that seems silly. idk, you're the boss. just make it sensible and fast.
  - (Answered 2026-10-12, todo entry 5) Threads are no longer capped: `--threads` defaults to every core. Checkers stay a fixed 4 by default because K changes output details (union order in messages) and memory; see vision.md "Thread defaults". Numbers from a many-core machine would tell whether a bigger fixed K pays.
- (2026-10-12, plan 55 session) genericCallOnMemberReturningClosedOverObject's expression lines: `example<number>()
  .foo("hello")` prints ten levels of `{ foo: <T2_1>(t2: T2_1) => ...` where tsgo's is `{ foo: <T2>(t2: T2) => any; ... }`.
  The object literal `x` instantiated inside its own generic method's signature instantiation (T2 -> fresh T2') is a new
  instantiation each level in tttt; tsgo keys an anonymous type's instantiations by its outer type parameters (T1 only,
  not the method's T2), so the same type comes back and is cyclic (`any`). Pre-existing (base build ff7d821a prints
  the same). Same family: `this` in an object literal method is the literal's widened type itself in tttt, another
  type in tsgo (getWidenedType(checkExpressionCached(literal))), see plans/55 "Known traps".
- **Source sweep flakiness** (agent B, 2026-10-12 fan-out): pixijs and feathers matched tsc in some full sourcesweep
  runs and not in others. Check whether their output depends on the checker count or file order (`scripts/checkerdiff.py`
  style: `--checkers 1` vs 4, twice each) before chasing their diagnostics.
- (2026-10-12, test/cases wiring) A JSX element spreading five unions of ten object types
  (`<Comp {...a} {...b} {...c} {...d} {...e} />`, Comp generic, `@jsx: preserve`) is TS2590 in tsgo, nothing in tttt
  (missed, before and after 16b4bb4b). The same five unions in `inter(a, b, c, d, e)` returning `P & Q & R & S & T`
  match tsgo. Probe: the jsx spread's union/intersection size check.
- (2026-10-12, test/cases wiring) TS2322 elaboration "Type 'SVGCircleElement' is missing the following properties from
  type 'HTMLSpanElement': ..." lists `accessKey, accessKeyLabel, ...` in tttt, `contentEditable, enterKeyHint, ...` in
  tsgo (react16.d.ts, test/cases/ttttDeferredCheckErrorLocation.tsx without its @ts-ignore; tiptap's MarkViewContent):
  the missing-properties list's order or filtering differs (both say "and 26 more").
