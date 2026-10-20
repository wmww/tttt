# Report
One short entry per insights (manager) session, oldest first: what happened, where the project stands, where it's going.

## 2026-09-18 — insights (first session)
The first AI session on the codebase. It inherited a ~17k-line C checker from a halted solo project: 829 of 11962 tests fully passed, and there was no perf benchmark and no tool showing why tests fail. It set up `agents/` (vision, todo, hacks, triage, plans).

Its main decision was that the foundation was wrong for the goal. Types were value-semantic trees with no identity, the relation cache was keyed by type strings, there was no control flow analysis, and scopes were thrown away after name resolution. It judged that replacing the type core would be cheap now and expensive later. It scheduled measurement tooling first, then a type core redesign (interned types with ids and id-keyed caches), then symbols and flow analysis. Its own doubt: the assessment came from skimming, so the planning session was allowed to overturn it.

## 2026-09-18 — insights (checkpoint mid type-core migration)
Since the first entry: two parser-gap sessions, measurement tooling (`failstats.py`, `bench.sh`), the type core plan, and migration stages 1-4a. The suite went to 1115 full passes. Every relation now ran on the new core, and a recursive-generics blow-up (38 GB) was gone.

Verdict: the design was holding up, since every stage raised the pass count and the suite ran 2x faster. The concern it raised was that each correct rule exposed the legacy code's remaining approximations and tempted blanket workarounds ("gave up -> any"), which all had to go by the final stage. It split the next stage into three and chose to rewrite generic inference on the new core rather than port the weak legacy version.

## 2026-09-18 — insights (after the type core migration)
The migration was finished: all types are interned `Ty` values, and the legacy type world was deleted over 8 sessions, each of which raised pass counts. The suite stood at 1434 / 13165. Compiler options now lived in one struct, and multi-configuration tests were supported.

The key finding was that tttt had never parsed a real program. Running it on tsc's own `src/compiler` (146k lines) gave 1119 syntax errors and then heap corruption. The perf headline so far had come from a synthetic corpus of tests tttt already passed. The session added `realworld.py` and made "zero diagnostics on a real project" a second oracle next to the test suite. It split the next work into a new binder/symbols plan (04) followed by flow graph and narrowing (05).

## 2026-09-22 — insights (corpus stalling)
Since the last entry: the real corpus now parsed and typechecked end to end (plan 03). Plans 04-06 landed: a tsc-shaped binder, a flow graph with narrowing, and module resolution with tsconfig. The suite rose to 1971 / 13163, and the tsc corpus ran in about 0.65 s against 0.72 s for tsgo.

The concern was that corpus diagnostics had been flat at about 1750 across three plans while the suite rose, which is the test-chasing pattern the previous session warned about. Most of that count was one contextual-typing bug (810 diagnostics in one file). It wrote plan 08, "corpus to zero", to work root cause by root cause, preceded by a budgeted plan 07 to fix types-baseline underline fidelity.

## 2026-09-22 — insights (after plan 08)
The tsc corpus reached 0 diagnostics. With all checks running, tttt was faster than multi-threaded tsgo while itself running single-threaded (0.88 s vs 1.02 s, lower memory). Plan 07 turned the types component into a real measurement, and the suite reached 4211. `this` types, the iteration protocol and generators, higher-order generic inference, and literal freshness with excess property checks all landed, each in one session.

Direction: that corpus is ES5-era code with little type-level programming, and the type core still lacked template literal types, key-remapping `as` clauses and substitution types. So a second, modern, type-heavy corpus came next (plan 17), followed by the cheaper suite plans (syntax gaps, member modifiers, error elaboration, missing checks). JS/JSDoc, JSX and decorators were deliberately postponed.

## 2026-09-24 — insights (after plan 13)
Eight plans finished in eight sessions with no redesigns. They added two modern corpora (type-fest, valibot), template literal types and `as` clauses, a conditional-type instantiation cache (valibot 4.4 s -> 1.2 s), member modifiers, and error elaboration. The suite went 4211 -> 5116. Corpora: tsc 0, type-fest 450, valibot 1871. tttt led tsgo everywhere except valibot.

It analysed where the ~8000 failing tests are. About 2350 fail only because diagnostics are missing (checks never run), and about 2600 pass errors but print types differently from tsc. It also found that `.tsx` tests had never been in the suite, so JSX was invisible to the pass rate. The chosen order was core type rules from the corpora, relation-cache correctness bugs, missing checks, a display-fidelity plan, then `.tsx` tests and JSX. The concern was that the easy suite plans were running out, and the big remaining areas should be planned rather than done piecemeal.

## 2026-09-26 — insights (after plan 23)
The suite reached 8202 / 13540, now including JSX tests. TypeScript-only tests were at 64% (8045 / 12491), JS tests at 157 / 1049. Missing checks, display fidelity, JSX, binding patterns and symbol merging all landed. Corpora: tsc 0, type-fest 168, valibot 164. tttt was ahead of tsgo on all three, single-threaded.

Concern: the corpora had been flat for about 30 sessions because nobody was scheduled on them. Every todo window now includes corpus sessions, and a React corpus became the third oracle (plan 25). JS/JSDoc got a planning slot (892 of the failing tests, and its binder side is foundational). Parser error recovery got an architecture question (613 TS tests fail on a spurious syntax error). On process: the insights cadence had drifted, and four sessions ended without committing. todo.md now has a fixed ten-session budget, and CLAUDE.md says that ending the turn ends the session.

## 2026-09-27 — insights (after plans 26-30 were written)
Ten sessions ran within budget, all committed, with nothing lost. The suite went 8202 -> 8579, with JSDoc core + CommonJS (JS tests 160 -> 247), parser list recovery, corpus work, and a perf check-up. Corpora: tsc 0, react 1 (from 362), type-fest 53, valibot 60. tttt was ahead of tsgo on all four corpora while single-threaded, for example react 0.30 s vs 1.06 s.

The remaining corpus diagnostics are structural. They need two ports of named tsc mechanisms (lazy mapped templates / deferred interface references, and indexed-access simplification plus substitution types), planned with measured prototypes and scheduled first. The existing corpora were running out of signal and nothing checked server-side code, so plan 31 adds a fourth corpus (a Node app, decorator-heavy if possible). Remaining suite failures are a long tail spread over hundreds of codes, to be worked in "suite checker bugs" sessions. Permissive-grammar parser work was deferred, at a possible cost of about 100 tests. It also flagged that hacks.md (914 lines) needed pruning.

## 2026-09-27 — insights (after the window of plans 29-31)
Another clean window: ten sessions, all committed, nothing lost. The suite went 8579 -> 8952 of 13540 (66%), JS tests 247 -> 299. Both planned type-core ports landed, a fourth corpus (a NestJS server app) went from 134 diagnostics to 0 in a day, and valibot went 60 -> 0. Four of five real-world corpora now report nothing tsc doesn't; type-fest has 19, all with known causes. tttt is still 2-5x faster than multi-threaded tsgo on every corpus while running on one thread, except the NestJS app (0.31 s vs 0.58 s), where parsing dominates.

Direction: the corpora are out of signal, and none exercises JavaScript with JSDoc types, which is the next big feature area. So the next window starts by pinning a checkJs project (webpack or svelte) and lets it order the JSDoc work. I also found a bucket no one owned: 431 failing tests report exactly the right errors at the right places but with different message text (error elaboration chains, type printing in messages). It gets a tool and a plan, the way earlier buckets did. The parser is now the top item both for speed and for spurious syntax errors; this window has a parser perf session and the permissive-grammar stage.

Concerns are mild: the planning notes are growing (two files are mostly history) and the remaining suite failures are a long tail with no single big lever left, so progress per session will be steadier and smaller from here.

## 2026-09-27 — insights (after the window of plans 32-34)
A third clean window: ten sessions, all committed, nothing lost. The suite went 8952 -> 9409 of 13540 (69%), JS tests 299 -> 395. Two new real-world corpora were pinned: webpack (checked as JavaScript with JSDoc types, 6146 diagnostics at first, now 135) and zod (inference-heavy, 87 -> 15 the same day). tsc's compiler, valibot, react and the NestJS app stay at 0, type-fest is at 4. Parser fidelity and error message text each got a tool and a plan, and both buckets shrank by about a quarter.

Two concerns set the next window. First, the types component now fails more tests than any other (2797), and most of those differences are not about printing: tttt computes a different type and no error shows it. Nothing owned that bucket, so plan 35 builds a tool to name its mechanisms. Second, performance margins are narrowing. On webpack, tsgo now beats tttt even on one thread (0.60 s vs 0.69 s), and the NestJS app is within 35% of multi-threaded tsgo. So this window has a perf session aimed at webpack and zod, and a planning session (plan 36) to prototype parallel parsing and audit what blocks multi-threading. Until now parallelism had been postponed as "later".

## 2026-09-28 — insights (after the window of plans 32 B-36)
The best window so far: ten sessions, all committed, nothing lost, and the suite went 9409 -> 10215 of 13540 (75%),
with JS tests 395 -> 519. Six of the seven real-world corpora report nothing tsc doesn't (type-fest and zod reached 0
this window). webpack, checked as JavaScript, is at 107 (from 135). tttt now beats even single-threaded tsgo on every
corpus. Sessions that fan out small independent fixes to parallel sub-agents delivered about twice what one agent
does.

The concern is a pattern in the remaining long tail. More and more fixes stop at "this needs tsc's mechanism X, which
tttt approximates", and then add another workaround around X: the missing type behind exactOptionalPropertyTypes,
null/undefined widening, evolving arrays, decorators checked as calls, union origins. So the next window starts
porting these properly (plan 37), beginning with a planning session that measures and ranks them. This is how the
earlier type-core ports went, and they paid off.

It also starts building parallelism. The design question was settled in favour of a fixed number of logical
checkers, as tsgo does, so output doesn't depend on the thread count. Parallel checking comes first, since it is the
big win (about 3x on webpack's check phase at 4 threads); parallel parsing follows later.

## 2026-09-28 — insights (after the window of plans 36-37)
Another clean window: ten sessions, all committed, nothing lost. The suite went 10215 -> 10543 of 13540 (78%), and JS
tests went 519 -> 560. The gain is smaller than last time on purpose, because two sessions went into parallelism. tttt
now checks on 4 threads, and its output doesn't depend on the thread count. It runs 2-6x faster than tsgo on every
corpus (tsc's compiler 0.53 s vs 1.26 s, zod 0.45 s vs 2.92 s). Six tsc mechanisms tttt had been approximating were
ported (plan 37), and each landed at exactly the gain its prototype measured. webpack went 107 -> 30 diagnostics, and
zod's root config now reports exactly tsc's 55 errors.

The next target is the same pattern one level deeper. Generic call inference (which type parameters get fixed when,
and how callbacks get their contextual types) is now where approximations are stacked on each other. It is also
webpack's largest remaining cause. So the window pins an inference-heavy real-world project first, then plans the port
(plan 38) with prototypes. This port is riskier than plan 37: tsc re-checks arguments, and tttt memoizes per node. The
planning session is allowed to conclude it needs groundwork first. The rest of the window works the long tail with
parallel sub-agents (suite bugs, JS, parser, type printing), plus parallel file loading.

## 2026-09-29 — insights (after the window of plan 38)
The biggest window so far: ten sessions, all committed, nothing lost, and the suite went 10543 -> 12040 of 13540
(89%), with JS tests 560 -> 898. Most of the gain came from sessions that split the failing tests into buckets and gave
each to a parallel sub-agent. The port of tsc's generic call inference (plan 38) landed as planned. A new
inference-heavy corpus, xstate, went from 394 false errors to 19. Files now load in parallel.

The remaining 1500 failures are a thin long tail (each failing test matches tsc on 99.5% of its lines). The biggest
gaps left for real users are whole features. Libraries built with `declaration: true` get diagnostics from tsc that
tttt doesn't produce, and monorepos use project references (`tsc -b`), which tttt ignores. The next window plans both,
along with two deeper tsc mechanisms (one circularity stack, resolving each call once). It also pins two new
real-world corpora, because the current ones have almost nothing left to find.

One concern: tttt has become about 8% slower (by instruction count) over roughly 20 sessions of fixes, with no perf
session on the checker in between. It is still 2-6x faster than tsgo, but the next window includes a perf session to
recover this before the new features add more.

## 2026-10-04 — insights (after the seventh window)
Another clean window: ten sessions, all committed, nothing lost. The suite went 12040 -> 12354 of 13540 (91%). That's a
smaller gain than last time, as planned, since four sessions went into planning and measuring. Tests failing on
errors alone fell from 810 to 427. Two new real-world projects were pinned and nearly cleared: drizzle-orm went from
693 false errors to 6 and vue from 107 to 0. The ~8% slowdown flagged last time was reversed and then some, mostly
by adding caches tsc has and tttt lacked (tsc's compiler sources: 5.2 -> 4.5 billion instructions).

The next window builds two whole features that real users hit first and that are now fully planned. The first is
the diagnostics tsc reports for libraries built with `declaration: true`. The second is project references
(`tsc -b`, monorepos), with TypeScript's own source tree as the test project. These also make the speed comparison
fairer, because tsc does declaration checking on four of our test projects and tttt doesn't yet.

The main concern is a pattern across several sessions. tttt computes some things as soon as a type is created, where
tsc only computes them when asked. Each session that ran into this patched around it locally, so the workarounds
are piling up. A planning session will decide whether to switch to tsc's lazy approach. That might also fix the
other weak number: memory use is above tsgo's on most projects, even though tttt is 3-11x faster.

## 2026-10-05 — insights (after the eighth window)
Another clean window: ten sessions, all committed, nothing lost. The suite went 12354 -> 12753 of 13540 (94%). The
two planned features landed. tttt now reports the diagnostics tsc gives libraries built with `declaration: true`, and
it builds monorepos with project references (`tttt -b`). That covers TypeScript's own source tree (1.3 s, vs 2.3 s
for tsgo and 13.6 s for tsc) and typescript-eslint, both with zero false errors. Speed stayed flat while these went
in, and memory is now below tsgo's on every project except two, which have a plan.

The concern this time is the oracles, not the code. Every real-world project we test on is at or near zero false
errors, and the failing tests are spread thinly over ~190 different error codes. More of the same finds less and
less. Each time we've added a new real project, it has turned up tens to hundreds of bugs. So the next window adds
two more projects that exercise what the current ones don't: Effect, a library that is very heavy on generics, and
a large application for scale. It also plans a "breadth sweep" that runs tttt over hundreds of small typed packages
at once. The rest of the window finishes the planned work: the remaining laziness stages, memory for the two
projects still above tsgo, and parallel name resolution.

## 2026-10-05 — insights (after the ninth window)
Another clean window: ten sessions, all committed, nothing lost. The suite went 12753 -> 13099 of 13540 (96.7%). The
bet from last time paid off. Effect (299k lines) started with 6300 false errors and is down to 7. VS Code's source
(1.39M lines) started with 129 and is now at 0, and tttt checks it in 2.2 s and 2.1 GB, against 6.75 s and 3.7 GB for
tsgo. A new "breadth sweep" runs tttt on all ~8600 DefinitelyTyped packages in a few minutes: 99.8% now match tsc
exactly, up from 97%. Memory use is now below tsgo's on every project.

The concern now is what these tests can't see. Every real project we test on compiles cleanly under tsc, so all they
can catch is tttt reporting errors that don't exist. They can't catch tttt missing an error, which is what matters
most to anyone relying on it. This session tried a cheap fix: recheck the same clean projects with stricter compiler
flags, which turns them into thousands of real errors. On TypeScript's own sources, tttt matched about 98% of these,
and the rest were genuine bugs (one flag's two specific error codes are never reported at all). The next window
builds that into a tool, and considers "mutation testing" (breaking clean code on purpose and comparing). It also
builds a similar check for inferred types across DefinitelyTyped. It plans one compiler mechanism that the codebase
currently works around with hacks, pins two new kinds of real projects, and includes a perf session.

## 2026-10-07 — insights (after the tenth window)
Another clean window: ten sessions, all committed, nothing lost. The suite went 13099 -> 13303 of 13540 (98.2%).
Last time I worried that our tests could only catch tttt reporting errors that don't exist, not errors it misses.
This window built two tools for that. One rechecks the real projects under stricter compiler settings, which gives
53k real errors to compare against. The other breaks passing tests on purpose and compares. Together they found
about 3000 mismatches, now down to about 100. Two new projects (a large React app and a router library) went to zero
false errors the same day they were added. Speed is unchanged or better: two projects got 13-37% faster.

The concern is bigger this time. Everything we compare against is TypeScript 5.8, from February 2025. TypeScript 6.0
(March 2026) and 7.0 (July 2026, the Go rewrite we benchmark against) have both shipped since. 6.0 changed defaults,
such as strict mode on, and deprecated old options. Our test projects are frozen at the last commits before their
authors moved to 6.0. At 98% on 5.8, more polish on that version is worth less than supporting the language people
use today. So the next window switches the target. A planning session will measure the new test suite (about 12k
test files changed, mostly mechanically). It will also decide whether to match 6.0 exactly or 7.0's new rule for
ordering the types it prints, which could remove a source of fragility. Then come the switch itself, re-pinning the
real projects at current versions, and a fair speed comparison against TypeScript 7. Expect the pass count to dip
before it recovers.

## 2026-10-07 — insights (after the eleventh window)
The switch to the current TypeScript went better than expected: it took one window, not the dip-and-recover I
predicted. tttt now targets the TypeScript 6 commit that TypeScript 7 (the Go rewrite) implements, including its new
rule for ordering types, which let us delete some fragile code. 14829 of the 14969 tests in the new suite pass
(99.1%), every real project was moved to its current version and is back to zero false errors (except Effect at 3),
and tttt is 3x (VS Code) to 14x faster than TypeScript 7 with less memory on every project.

The concern is diminishing returns. The last 140 failing tests each cost a special case, and the list of workarounds
in the code keeps growing while the pass count barely moves. Many of those workarounds come from one place where tttt
approximates how TypeScript handles function arguments inside generic calls, and it gets patched again almost every
session. So this window stops chasing individual tests. It plans and lands a proper port of that mechanism, audits
the workarounds list to delete the ones that are no longer needed, and lands one planned binder fix (plan 50). It also
builds a new check that runs tttt on a hundred or more real TypeScript repositories at once, since every new real
project has turned up bugs the tests never showed. One perf session targets VS Code, where our lead over TypeScript 7
is smallest.

## 2026-10-08 — insights (after the twelfth window)
A good window. Last time's worry was that we kept adding workarounds while the pass count barely moved. This window
replaced the biggest cluster of them with a proper port of how TypeScript types function arguments in generic calls,
and an audit deleted about 20 more. The workaround list shrank for the first time in four windows, and nothing broke:
14835 of 14969 tests pass, and every real project is still at zero false errors (except Effect at 3). We also built a
new check that runs tttt on 3359 projects from 174 real repositories. It found bugs and slow spots nothing else did,
and two sessions took it from 2241 to 3003 projects that match TypeScript exactly.

The next window is your request: target TypeScript 7 and make tttt a drop-in `tsc`. First, tttt's command line output,
options and exit codes will match tsc's. Then we switch what we test against to TypeScript 7.1's own test suite and
compiler, pinned to a nightly build for now and moved to 7.1.0 when it ships on 2026-11-24. We'll delete the options
and JavaScript features TypeScript 7 removed. Expect the pass rate to drop to around 90% at the switch, mostly in
JavaScript tests, and then recover. The main risk is the testing tools, which will have to be rebuilt around
TypeScript 7's Go compiler. The type checker itself barely changes.

**A question for you:** should emitting output (`.d.ts` files and/or JavaScript) be in scope? Most people run `tsc` to
emit files, so a checker-only tttt is a drop-in only for `tsc --noEmit`. Until you say otherwise, emit and watch mode
stay out of scope and the readme will say so.

## 2026-10-11 — insights (after the thirteenth window)
The switch to TypeScript 7 went better than planned. tttt now targets TypeScript 7.1 (a nightly build for now; we'll
move to 7.1.0 when it ships in late November) and is tested against TypeScript 7's own test suite and Go compiler.
13316 of its 13571 tests pass (98.1%). The pass rate fell to 87% at the switch and was back above 98% within two
sessions. Its command line now behaves like `tsc --noEmit`: same output, byte for byte, and the same exit codes. The
features TypeScript 7 removed were deleted (about 2,200 lines of JavaScript support), and the list of workarounds kept
shrinking.

The concern is speed, which is the actual goal and was set aside this window. A new competitor, Bun's `bun check`
(a Rust port of TypeScript 7), uses about a third of tttt's memory on large projects and matches our default speed on
a 12-core machine. Also, our "3-14x faster than TypeScript 7" figure comes from 14 projects we've tuned for months.
A quick look at the 3363 real projects we also check found waste those 14 never showed. tttt type-checks programs
that tsc refuses to check because of config errors, which is about a quarter of its time on that set. On some
projects it also creates 2-3x more types than TypeScript 7. So the next window is perf-first: measure tttt against
TypeScript 7 on all of those projects and fix the outliers, cut memory, and redo the speed benchmark. Correctness
work goes on alongside.

**Two questions for you:** (1) Still open from last time: should emit (`.js`/`.d.ts` output) be in scope? (2) tttt
uses at most 4 threads by default, like TypeScript 7. Bun uses every core, and at 12 threads tttt beats it
everywhere but uses more memory. Our dev machine has 4 cores, so agents can't measure scaling past that. If you can
run `scripts/bench.sh`-style numbers on a bigger machine after the memory work, we'll pick the default from them.

## 2026-10-13 — insights (after the fourteenth window)
The speed-focused window worked. We measured tttt against TypeScript 7 on 1779 real projects, not just the 14 we've
tuned for months, and fixed what turned up: about 20 caches and shortcuts TypeScript 7 has that we lacked. The worst
case went from 16x more internal work than TypeScript 7 to 1.6x. No project now uses more memory under tttt than
under TypeScript 7, and the largest project (VS Code) uses 16% less memory than before. Typically tttt checks a
project in about a sixth of TypeScript 7's time. Correctness work continued alongside: 99.1% of TypeScript 7's own
tests pass (up from 98.1%), and 3307 of 3363 real projects match TypeScript 7 exactly (up from 3119).

Against TypeScript 7, the goal is met. The remaining competitor is Bun's `bun check`. tttt is 1.1-2.7x faster at the
same thread count, but Bun uses 2-3x less memory, and it uses every core while tttt stops at 4. Because of that,
Bun matches us on VS Code on a 12-core machine. The two causes are structural, so the next window plans before it
builds: (1) whether tttt's output can stop depending on its thread count, so it can safely use every core, and
(2) a cheaper way to store the parsed syntax tree, which is most of our memory. Also planned: one last rewrite of a
core type rule we still only approximate, a round of real-project bug fixes, and a cost estimate for generating
`.d.ts` files. The human left `.d.ts` output to us. It's the one feature that would make tttt a drop-in for more
than type checking, and we'll decide on it with a size estimate. We'll switch to TypeScript 7.1.0 when it's
released (expected after 24 November).

**A question for you:** `AGENTS.md` used to be a copy of `CLAUDE.md` but stopped being updated on 2026-10-08. It
also has one line that `CLAUDE.md` lacks: the `claude-stop` file that ends your dev loop. Is it still read by
something? If so, should we keep it in sync, or should it be a symlink (with the `claude-stop` line moved into
`CLAUDE.md`)? We've left it alone for now.

## 2026-10-14 — insights (after the fifteenth window)
**Disk:** you were right, most of it was us. A Go build cache (from building patched copies of TypeScript 7 to
compare against) had grown to 50 GB. It's cleared (the disk went from 98% to 76% full) and now limited to 4 GB.
Sessions are now required to delete their scratch files and temporary worktrees before they end. What's left is
needed: about 10 GB of test projects plus the ~150 real repositories we check against.

The window went to plan. The last core type rule we only approximated is now TypeScript 7's exact algorithm.
tttt's output no longer depends on how many threads it uses, except in 4 of 3363 real projects. That is the
precondition for using every core like Bun does. Five cases where very large files took quadratic time are
fixed. 99.2% of TypeScript 7's own tests pass, and 3332 of 3363 real projects match it exactly. The one miss
is memory: trimming the syntax tree saved 6% on VS Code, short of the 10% target. Bun still uses about half
our memory, and small trims won't close that gap.

**Next window:** your "100% green" request comes first. That means a bug-fixing sweep, then support for
TypeScript 7.1's content mappers (10 tests), then a second sweep. About 12 tests only pass once tttt can write
`.d.ts` files, because TypeScript's test harness writes them before it reports errors. So `.d.ts` output gets
built to a working first version this window. tttt will also start using all cores by default. One session will
plan a structural fix for memory before we write more memory code. If some test turns out to be impossible to
pass honestly, we'll report it here with the reason instead of special-casing it.

## 2026-10-15 — insights (after the sixteenth window)
On your "100% green" request: 13556 of TypeScript 7's 13568 tests pass (99.9%, up from 99.2%). We're stopping
there until TypeScript 7.1.0 is released and we re-pin. Of the last 12, five are cases where the expected output
depends on how TypeScript's test harness runs. Their order of work differs from what the real `tsc` does, and the
real `tsc` agrees with us on those. One is a caching bug in TypeScript itself. Each of the other seven needs a
multi-session mechanism to pass one test. The last push to fix such tests made our hack list 13% longer. Chasing
these seven further would mean imitating TypeScript's internals rather than its behaviour. The reasons are in
`agents/bugs/tsgo_divergences.md`.

Real code improved along with the tests. Every pinned project now matches TypeScript exactly except two missing
diagnostics in Vue, and 3344 of 3363 real projects match. tttt now uses every core by default, and its output stays
identical at any thread count. `.d.ts` generation went from nothing to a working writer in three sessions: 97.6% of
the files it produces across ~1200 real projects are byte-identical to TypeScript's. It doesn't write files from the
command line yet.

**Next window:** finish both of those. Memory comes first. A prototype that periodically discards and restarts the
type checker cut VS Code's peak memory by about 40% with identical output, and it gets built properly first. After
that, `tsc --emitDeclarationOnly`-style output with declaration maps, then trimming the syntax tree's memory. One
session will audit and pay down the hacks the last test push added, and one will plan how to use 12+ cores well.
Still open for you: the 12-core benchmark in `agents/triage.md`.

## 2026-10-16 — insights (after the seventeenth window)
Both half-built things shipped. tttt now periodically discards and restarts its type checker, which cut VS Code's
peak memory by 40% with identical output. As a drop-in `tsc` it writes `.d.ts` files and their source maps: 1077 of
1233 real projects are byte-identical to TypeScript's, and most of the rest have JavaScript sources, which come
next. One session halved the hack list by deleting hacks rather than documenting them. 3352 of 3363 real projects
and every DefinitelyTyped package now match TypeScript exactly. Thanks for restarting the loop; `claude-stop` stays
out of git.

The checker is essentially correct on real code, so the open questions are about the product. tttt is 2-6x faster
than single-threaded TypeScript 7 and 1.3-3x faster than Bun's checker. It still uses 1.6-2.3x Bun's memory. Most
of that is how the syntax tree is stored. Each extra core also adds its own checker memory. This window builds
cheaper per-file allocation, then prototypes a compact syntax tree before we commit to a multi-session rewrite.
Separately, tttt refuses `--watch` and `--incremental`, which is how many people run tsc day to day. A planning
session will measure whether our full re-check already beats TypeScript's incremental rebuild. It will then bring
you a scope question, like the one you answered on JS emit.

Still open for you: the 12-core benchmark in `agents/triage.md`. Scaling past 4 cores is the one perf claim this
machine can't check.

## 2026-10-16 — insights (after the eighteenth window)
The window shipped what it planned. Allocating each file's syntax tree in its own arena took VS Code from 8.1 to 5.9
s at the defaults on the 4-core box (and found that asking the kernel for huge pages past what it has free cost a
third of the wall time in compaction stalls). Your 12-core run put VS Code at 2.5 s against Bun's 2.9 and TypeScript
7's 8.0. `.d.ts` output now covers JavaScript sources: 1193 of 1233 real projects are byte-identical to TypeScript's
(from 1077). 3356 of 3363 real projects and every DefinitelyTyped package match; the 7 left are each argued or a
multi-session mechanism for one repository. The compact syntax tree was measured and declined: the empty bytes are
sparse optional fields on every node kind, not padding, so only a rewrite would move them.

Every oracle is now saturated, which worries me more than it reassures: the 174 repositories were chosen once and
drained over eight sessions. A plan to test generalization on ~30 repositories chosen differently is written (plan
77) and goes first after this window. The hack list grew by 50 lines, mostly honest "guesses" in the JavaScript
declaration emitter; the next window pays that down.

**Your decision this session: memory is the goal.** Less memory than every competitor in most cases, as fast as
possible, correctness unchanged, features (watch, incremental, the cheap `tsc` modes of plan 76) parked. We agreed
32-bit IDs and more struct diets can't get there (together ~300 MB of a 2.2 GB program on VS Code). The window is
built on the two levers that can: parsing function bodies only when a checker needs them and dropping them after
(bodies are most of the syntax tree; VS Code's program should fall from ~2.2 to ~1.1 GB), and recycling the type
checker by copying its declaration-level state instead of rebuilding it, so each checker can run on tens of MB
instead of 64-128. Both start with a measured go/no-go before any stage is built. The mechanical items you suggested
(boxing the large node variants, 8-byte spans with the file found from the node's address, mmapped source text) are
one session. The rule is that memory may cost at most ~3% speed and the window's perf session pays it back.

**For you, after the window**: a 12-core `scripts/bench.sh --runs 3 --bun PATH` so the memory table has the
many-core column we can't measure here.

## 2026-10-17 — insights (after the nineteenth window)
The memory window did what it set out to. The biggest change: function bodies are now parsed only when a checker
needs them and dropped afterwards. With source text released after use and a smaller per-checker budget, VS Code's
peak fell from 2.78 to 1.56 GB at the defaults (Bun: 1.55) and from 2.49 to 1.39 GB single-threaded (Bun: 1.41). tttt
now uses less memory than TypeScript 7 on every corpus. It is still faster than Bun everywhere, 1.35-4x. The cost was
about 3% wall time on VS Code, partly paid back by the perf session. Correctness didn't move: every oracle is unchanged.
The copying-recycle idea was measured and declined at its gate, as the plan allowed.

Your target is half met. Single-threaded, tttt is at or below Bun on 7 of 14 corpora and within 1.4x on the rest. At
the defaults (every core) it is below Bun on only 3 of 14, with mid-size projects at 1.5-2x (effect 605 vs 315 MB).
Those programs are small, so most of their memory is type-checker state, repeated once per checker. Tuning the budget
only trades speed for memory. The real problem is density: on effect, tttt spends roughly 740 bytes per type. The next
window measures this against TypeScript 7's own structures (plan 81), with a go/no-go gate. If it says no, the gap is
duplication across checkers, and we will look again at sharing declared types between them.

Also next: testing on ~30 fresh repositories, chosen by a different rule than the current sweep, to see whether our
saturated oracles generalize (plan 77). Two debt sessions are also scheduled. Deferred bodies added a new set of
invariants to keep: what a body's check writes outside itself must be undone when the body is dropped. Their oracles
exist, but the code should make those invariants hard to break. TypeScript 7.1.0 stable isn't on npm yet; the re-pin
waits for it. **For you**: the 12-core `scripts/bench.sh --runs 3 --bun` is still the one memory column we can't
measure here.

## 2026-10-18 — insights (after the twentieth window)
The memory work is finished, as far as its rules allow. At the defaults (every core) tttt now uses no more memory than
Bun on 8 of 14 corpora (VS Code 1.22 vs 1.56 GB), and no more than 1.2x Bun's on the rest. Single-threaded it is 6 of
14, within 1.4x. It is still faster than Bun on every corpus, by 1.3-4.2x. The lever was policy, not data layout:
measurement showed tttt's per-type cost already matches Bun's. The difference was that tttt kept four large checkers
alive, where Bun keeps re-deriving types. So the checkers now start small (16 MB each). That costs about 13% wall time
compared with never recycling, and a perf session won back 3-7% of instructions (drizzle -34%, from a regression the
new instruction-count check now flags). I've accepted that trade. At the defaults the rule is now "stay faster than Bun
everywhere"; single-threaded it is still "at most 3% slower".

Correctness held. Every oracle is unchanged. The suite is at 13792 / 13829. 28 fresh repositories, chosen by a new
rule, matched TypeScript 7 on 96% of their projects on day one and on all of them after one session. That is good
evidence that the checker generalizes. Balance across checkers now uses work stealing, and the triage removed the last
deferred-body restarts.

The pattern I'm watching is re-derivation. Every checker rebuilds the same declared world (lib, React, Prisma
typings). It is what the memory budget costs, and it is why a few apps are slower than Bun at four cores. TypeScript 7
and Bun pay this cost too, so it is not urgent. But it is the main risk on many-core machines. Before planning a
shared layer, I'd like your 12-core `scripts/bench.sh --runs 3 --bun` numbers. The next window is debt: a small memory
leak in deferred bodies, optional types carrying `undefined` the way tsc stores them, re-entrant member resolution, and
tsgo's two-phase program load. It also includes a fan-out on declaration emit (our least saturated oracle at 96.8%), a
second refresh of fresh repositories aimed at libraries and checkJs projects, and a perf session. **A question for
you**: memory was "first, features later". Now that memory is done, should `--watch`/`--incremental` (plan 76, parked)
come back?

## 2026-10-18 — insights (after the twenty-first window)
This window paid down debt, and it worked. Three representation hacks are gone, each replaced by the rule tsc
actually uses: optional properties now carry their `undefined`, member resolution can be re-entered, and files are
collected in two phases the way TypeScript 7 does it. The deferred-body memory leak is fixed. A second batch of 22
fresh repositories (libraries that emit declarations, and checkJs JavaScript) matched TypeScript 7 on 97.1% of their
projects on day one. The suite is at 13833 / 13869, and every other oracle held.

Your point about "saturated" oracles was right. Message text is now an oracle of its own: tttt's whole CLI output
compared byte for byte with tsc's on every corpus and source-sweep project. 3612 / 3675 projects match exactly; 54
differ only in message text and 9 in their diagnostics. webpack and drizzle now print tsc's exact bytes. That number
will sit in this report next to the others. Declaration emit is at 1244 / 1274 projects, and 6 of the 30 that differ
come from tsgo's own multi-checker split.

Your 12-core numbers answered the open question, and the answer is uncomfortable. tttt is the fastest tool on every
corpus, at the defaults and single-threaded. Single-threaded it uses 2-2.5x less CPU than Bun or tsc-rs. But at the
defaults it spends 3-7x its own single-threaded CPU (drizzle: 6.4 s against 0.85 s), and it uses 1.2-1.8x Bun's
memory (Bun is lowest on 10 / 14). The cause is that every checker rebuilds the same declared world, and recycling
makes them rebuild it again. Budget tweaks have been tried five times and only trade memory for CPU. So the next
window measures re-derivation directly and decides, with a go/no-go gate, whether to build a shared declared layer
(plan 85). It also sets a many-core checker-count policy and adds a CPU column to the bench. I'll track tsc-rs next to
Bun from now on: it is a Rust port of tsgo at our pin, matches tsgo's output exactly, and spends the least CPU at its
defaults, which is precisely our weak spot.

**Still open for you**: whether `--watch`/`--incremental` (plan 76) should come back. Once entry 1 lands, a 12-core
rerun of the bench with the new CPU column would also help.

## 2026-10-19 — insights (after the twenty-second window)
On correctness, this window went into the long tail, and it worked. clidiff (tttt's whole CLI output compared byte
for byte with tsc's) went from 3612 to 3823 / 3831 projects, and the text-only mismatches went from 54 to 1.
Declaration emit is at 1325 / 1350. A third batch of fresh repositories (`tsc -b` solutions and decorators) matched on
98.1% of projects on day one. The three refreshes so far scored 96.1%, 97.1% and 98.1%, so the checker generalizes. The
source sweep now checks every `tsconfig*.json`, not just `tsconfig.json`: 5225 / 5242 projects match. There is also a
new `-b` oracle at 400 / 416 solutions. The suite is at 13928 / 13963.

On perf, the shared declared layer (plan 85) is a no-go. Rebuilding declaration files is only 2-6% of the extra CPU at
the defaults. What really gets duplicated is instantiations of each project's own code. The default checker count is
now capped at the physical cores, and the bench reports CPU. Your many-core question has become plan 86, and the next
window is mostly that plan. It ships PGO + LTO first (-8-9% measured), then measures whether a shared type graph could
keep the output byte-identical. Only after that does it start the single-threaded memory-system work, which is where
the multiples come from.

One design decision: `tttt -b` types referenced projects from their sources, while tsc reads their emitted `.d.ts`.
This window approximated the `.d.ts` view three times, and the remaining divergences can't be approximated at all. A
planning session will prototype reading tttt's own emitted declarations instead (plan 87), and it has a go/no-go gate.
A small robustness concern: a file truncated mid-run can crash tttt (mmapped sources). That is queued for triage.

**For you**: with PGO + LTO in, a 12-core rerun of `scripts/bench.sh --runs 3 --bun PATH --tscrs PATH` would check the
gain and the checkers-per-physical-core default. Plan 76 (`--watch`/`--incremental`) stays parked unless you say
otherwise.

## 2026-10-19 — ask for the human (execution session, not an insights entry)
PGO + LTO ships as `scripts/pgobuild.sh` (`pgo/tttt`, plan 86 stage 1): 11-17% fewer instructions on every corpus, including vscode and tsc, which aren't in its training set. Output is unchanged. Could you rerun the 12-core table with it? It's `scripts/bench.sh --runs 3 --checkers 24 --bun PATH --tscrs PATH`, and bench.sh now builds and uses `pgo/tttt` by itself. The first run retrains, which takes a few minutes.

## 2026-10-20 — insights (after the twenty-third window)
This window was about perf, and you cut it short after four entries. PGO + LTO now ships (`scripts/pgobuild.sh`) and
takes 11-17% of instructions on every corpus, including the ones it wasn't trained on. Your 12-core rerun showed
tttt with the best wall time on all 14 corpora at the defaults, and it confirmed the checkers-per-physical-core cap:
K24 costs 40-160% more CPU for at most 7% wall. The gate for a shared type graph (plan 86 stage 4) said go. Union
ordering only falls back to type ids in 0.003-0.6% of ties, and permuting those ids changed no oracle's output. `-b`
reading its references' emitted declarations (plan 87) also said go: it was both more correct and faster
(tsc-src wall -23%). A real-code fan-out brought the source sweep to 5234 / 5242 projects, and the suite is at 13944
/ 13978. 7.1.0 stable isn't on npm yet.

The bun canary you measured (oven-sh/bun#45800) changes the picture. At the defaults it now beats tttt's wall on 4 /
14 corpora: vscode 2.00 against 2.44 s, plus drizzle, excalidraw and nest. It does this by keeping ~20 threads busy
at 1.7x tttt's CPU. tttt is still 1.1-2.9x faster single-threaded and uses less CPU everywhere. Bun's output also
isn't tsc's (21 errors on drizzle against tsc's 41). Even so, "faster than bun at the defaults" is broken. Following
your ask, I've reordered plan 86. The shared concurrent type graph (stage 4) goes next. It's the one lever that lets
extra checkers pay off instead of duplicating work, and it is also the one that cuts memory at the defaults, where
bun is lower on 10 / 14. The single-threaded memory-system work (stage 2) gained only ~2% in its first session, so it
drops to filler.

This is the riskiest change since the type core was rebuilt: every lazily computed type field has to become safe to
publish from any thread. So the window starts with a design session and a prototype on drizzle, with an explicit
go/no-go, before any build-out. If the answer is no, the fallback is the front end's serial floor (~0.6 s of
vscode's 2.6 s) plus a snapshot recycle for projects like supabase studio. Also in the window: deleting the old
`-b` model, starting each checker with its costliest file (bun's heavy-first idea, cheap), and one real-code session.

**For you**: nothing is blocking. Once the prototype exists, its 12-core numbers will matter more than anything this
4-vCPU box can measure, and the session will leave you a command to run.

## 2026-10-20 — ask for the human (execution session, not an insights entry)
Each checker now starts with its biggest file. On this 4-core box that's 6% off TypeScript6's compiler (checker.ts is
its critical path) and 2% off type-fest, with nothing else changed. The front-end split says the loader's serial work
(~280 ms on vscode) comes before parallel declare/link (~140 ms) in plan 86 stage 3. Both should matter more on 12
cores. Could you run this on the 12-core box? It A/Bs the same binary and prints vscode's front-end split:
```
B=$(scripts/pgobuild.sh); for c in TypeScript6/src/compiler type-fest drizzle/drizzle-orm/tsconfig.corpus.json vscode/src/tsconfig.corpus.json; do for off in "" 1; do for i in 1 2 3; do env ${off:+TTTT_NO_COSTLIEST_FIRST=1} /usr/bin/time -f "$c off=$off %e s wall %U+%S s cpu %M KB" $B -q --keep-going --noEmit -p thirdparty/corpora/$c >/dev/null; done; done; done
TTTT_LOAD_POOL_STATS=1 TTTT_RESOLVE_TIMES=1 $B -q --time --keep-going --noEmit -p thirdparty/corpora/vscode/src/tsconfig.corpus.json 2>&1 >/dev/null | grep -av '^time: checker'
```

## 2026-10-20 — the 12-core rerun (an agent on your laptop, not an insights entry)
I ran the costliest-first A/B and a full bench (bun canary and tsc-rs included) at efb91998; the numbers are in
measurements.md ("The 12-core box at efb91998"). Starting each checker with its biggest file is a wash on 12 cores:
the 6% it saved on the 4-core box doesn't show up here. vscode's front end (load, parse, name resolution) is ~0.5 s
of its 2.5 s, which confirms plan 86 stage 3's premise. The race with bun's canary is unchanged since yesterday: it
still wins wall at the defaults on vscode (2.05 against ~2.5 s), drizzle and nest. tttt wins the other corpora, is
1.5-7x faster single-threaded and uses less CPU everywhere. Nothing landed in between that should have moved it.
Plan 86 stage 4 (the shared type graph) is still the lever.
