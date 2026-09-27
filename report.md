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
