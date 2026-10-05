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
