# TypeScript 7.0 compatibility: sized and shelved (2026-10-23)

The human asked how hard a build-time TS 7.0.2 compatibility flag would be; the answer below (a read-only research
agent's report, lightly trimmed) is kept so the question needn't be researched again. **Decision (the human,
2026-10-23): shelved.** tttt ships 7.1 semantics (the oracle is 7.1.0-beta, 7.1.0 stable when it lands); the delta
to 7.0.2 on real code is small and listed under "What 7.0.2 vs the beta does on real code". If the question
reopens, the cheap visible part is option (b): the exit code (1 under 7.0.2 for errors, 2 under 7.1), the lib set and
default target, distributed type parameters off, TS2589 in place of TS5114/TS5115, the version strings.
The agent's scratch (the 333 differing suite configurations, the 7.0.2 corpora outputs) was in /tmp and is gone;
the method below regenerates it.

I estimate about 10 structural changes, 15 medium ones and about 75 small ones. A flag that copies every 7.0.2 behaviour would take about 20 to 30 worker-days to build, and a small permanent cost on every later change after that. The code is not the expensive part; keeping a 7.0 mode trustworthy is. It needs a second tsgo, a second set of suite baselines in another directory layout, and a second sample of the real-code caches. A standing decision in vision.md also says "no dual-version modes", so building this would reverse it.

## Method and what I ran
- **Commit list:** the GitHub compare from 1e4744d6 to 3e923636 shows 471 commits on the beta side and 10 on the 7.0 side, which are only release and version commits. I kept the changes that affect diagnostics, types, libs, resolution, declaration emit or the CLI.
- **Source and baselines:** I fetched both trees without file contents (one blob-less fetch, 1.1 GB, now deleted) and diffed the checker, binder, parser, libs, the diagnostic tables and the suite baselines.
- **Release notes:** the "Announcing TypeScript 7.1 Beta" blog post lists import attributes on ambient modules, source phase imports, `--target es2026`, new esnext lib methods, the API, content mappers, a DOM lib update and stricter checks on distributed type parameters. The GitHub release notes are almost empty (165 characters).
- **Real code under 7.0.2:** I downloaded the published npm binary `@typescript/typescript-linux-x64@7.0.2` into my scratch directory. I ran it with `--noEmit` on the 15 single-project corpora and on every 8th DefinitelyTyped package (1,075 packages), and compared against the beta caches. Nothing was written into the repository. Another agent's `oracles.py` was running at the same time and rewrote the corpora's `package.json` files at 12:07; that wasn't me.

**What 7.0.2 vs the beta does on real code:**
- **DefinitelyTyped sample:** 0 of 1,075 packages differ in diagnostics.
- **Exit code:** every project with errors exits 1 under 7.0.2 and 2 under the beta (fixed in 7.1 by #4407). This affects every corpus and package that has errors.
- **Corpora:** 10 of 15 are identical, including zod-root's 53 diagnostics. The five that differ:
  - **drizzle:** the beta reports 25 more TS2430/TS2416, from the distributed type parameters change (#64237).
  - **vscode:** the beta reports 10 more TS2345/TS2322/TS2416, all about `IConfigurationResolverService`. I didn't pin down the cause; distributed type parameters is the likely one.
  - **effect:** 3 more in the beta, 1 TS2345 only in 7.0 (`Schema.ts`).
  - **vue:** 4 more in the beta (the TS5115 runaway, two TS7006, `general.ts:133`).
  - **webpack:** 14 TS2565 only in 7.0 (a JavaScript regression fixed in 7.1 by #64523), and 1 TS5115 only in the beta.

**Suite:** about 13,000 test configurations exist in both versions.
- 333 differ in errors, types or symbols (167, 150 and 118 respectively). 277 of them have the same test input, so the compiler changed, not the test.
- 58 of the error differences are tsgo's own noise in the 7.0 baselines (`TS-1 Pre-emit/post-emit diagnostic counts`), which tttt cannot produce.
- 97 `.js` baselines differ, 63 of them in their `.d.ts` sections.
- 612 beta tests don't exist in 7.0, and 6 tests in 7.0 were dropped in 7.1.

## Changes and where tttt implements them

| Change | PR(s) | Area | tttt site(s) | Size |
|---|---|---|---|---|
| Distributed type parameters, TS5113 | #64237 | checker | 84164ef0f: marker in `type.c:455`; ~30 `ty_non_distributed()` call sites | local switch at the marker; the deviations listed in hacks.md need checking |
| Import attributes on ambient modules (TS1550–1558); attributes on type imports (7.0 has TS1463/1464 and TS2823/2856/2857); attributes typed `readonly "json"` | #63931, #64099, #64181, #64243 | parser, checker | 50ba62df9 (448 lines): `statement.c`, `import_attributes.h`, lowering | medium (~45 suite configurations) |
| Source phase imports (TS18111–18115, TS18061 text) | #63915 | parser, checker | 3691f4943 (15 files): `port_statement.c`, `expr.c` | medium: 7.0's parser recovery for this syntax must be matched, not just rejected |
| Deferred imports need a namespace binding (TS18117) | #64640 | grammar | `port_statement.c` | local |
| `es2026` target and lib; default target ES2026 (7.0: ES2025); TS6046 option lists | #64096 | options, libs | `compiler_options.c:22,284`, `lib_names.c`, `lib_features.c` | medium (generated tables) |
| Lib contents: esnext.iterator, Promise.allKeyed, PluralRules, DOM/webworker update, 9 `esnext.*` files renamed `es2026.*`; lib order | #64095, #64093, #63424, #64604, #64693 | libs | `thirdparty/lib` (21 files differ; 108 vs 117 files) | **structural (data)** |
| Regex: ES2025 syntax gated by target (TS18062/18063), lone `&`, TS1518, TS1515, escapes in group names | #4877, #4218, #4832, #4881, #63996 | scanner | `regex_check.c` (22a3895ec) | medium, all in one file |
| TS5114/TS5115 "appear infinitely circular" where 7.0 says TS2589 | most likely #4913 or #64461 | instantiate | `ty/instantiate.c:74-83`, plan 65 (d42dfc3c9, 292 lines) | medium |
| Recursion identities, `isDeeplyNestedType`, depth 100 is silent (7.0 reports TS2321) | #4913 | relate | `relate.c:6775,7211,8326` (a5e59fe32, be6e1d1b6) | medium |
| Optional `undefined` members keyed by symbol (7.0: by name) | #64728 | checker | 3baaad420 deleted ~380 lines of the 7.0 emulation (`program.c`, `ty/members.c`) | **structural** |
| Union order fixes (bigint literals by value, etc.) | #64280 | ordering | `ty/order.c` | local to medium |
| `.types` of interface heritage names: `any` in 7.1, `typeof X` in 7.0 | #4797 | suite types baseline only | `test/baseline_test_traversal.c:1107` | local (~15 tests) |
| Repeated declarations deduplicated on union/intersection properties | #63908 | symbols baseline | union property synthesis | local (~10 tests) |
| Computed property names always checked (new TS2552/TS2304) | #64674 | checker | one gate | local (~8 tests) |
| Small diagnostic fixes: TS7059 in .mts/.cts, TS2871, TS2454, TS1293, TS18064, TS5116, TS7080, TS9019 text, TS18116, unused locals in static blocks, `as`/`satisfies` between `**`, JSX attribute recovery, await context, dotted private names in type queries | #4746, #4586, #63984, #4800, #64084, #64832, #4775, #64485, #4676, #4823, #4898 and others | parser, checker | `parser.c:11917,9665,11312`, `expr.c`, `scope.c`, `binding.c`, `statement.c`, `declaration_emit.c` | ~20 local |
| JavaScript and JSDoc: TS2565 regression (7.0 only), TS8023, `@overload` TS7010 vs TS7012, JSDoc predicates, `@extends` type arguments, JSDoc modifiers, jsxImportSource in JS, CommonJS shadowed names, TS7023 removals | #64523, #4889, #3871, #64303, #64072, #64696, #4694, #64676 | JS | 6be489d30, 5ca125d38, 126481406, 5b91fb76c, aedbfc35a | ~10 local |
| Inference and type fixes: matching-type depth, constructor references, dependent destructuring, optional properties under exactOptionalPropertyTypes, binding defaults, `never` readonly tuples, intersections, recursive-call inference and constraint checks, static property circularity, keyof simplification, `anyFunctionType` leak, instantiation-expression symbols, template literal cap | #4591, #4691, #4606, #4598, #64043, #64389, #64481, #64372, #64530, #64311, #64525, #64772, #64131, #64123, #64194 | checker | `infer.c:1435`, `expr.c:2493`, `checker.c:3093`, `relate.c:4335`, `call.c` | ~15 local, some medium |
| Declaration emit: late expandos, `import()` specifiers, binding patterns kept, enum computed names, JSDoc, TS5088 cycles and truncation (10 suite diagnostics), reverse mapped types, `export=` visibility, expando export modifiers, declaration maps, package reachability, stripInternal, quote escaping, re-export index, symlinks | #4535, #4662, #4775, #4852, #4873, #4839, #64461, #64556, #64558, #64573, #64545, #64460, #64599, #64770, #64839, #64469, #4813 | dts | `ty/declaration_emit.c`, `module_specifiers.c` (bb7ee1f66) | ~18 local (dts component: 63 tests) |
| Resolution and program: own ambient module imports, node_modules depth, resolution-mode reuse; typesVersions version "7.0.2" | #64491, #64632, #4792 | program | `program.c:2154`, `module_resolution.c:617`, `main.c:1511` | local |
| CLI and config: exit code 1 with `--noEmit`; TS5092 location; misplaced root options; option spelling suggestions; malformed properties; project reference validation; file include reasons (TS6059 chain); FORCE_COLOR | #4407, #4760, #4761, #4759, #4762, #4494, #63941 | CLI | `main.c:1184`, `tsconfig.c` | ~8 local |
| Content mappers and `runExternalCode` (TS18065–18123) | #4712, #63936, #64581 | config | `content_mapper.c` (1,030 lines) compiled out, plus the option table | local |
| History-dependent output in 7.0: checker assignment, variance order, merged diagnostic ownership, inference order, never-reduction order, emit-resolver extra diagnostics (the 58 noisy baselines) | #4313, #4820, #64566, #64621, #64521, #64479, #64482, #64636 | — | — | **not emulable** (or only by tsgo-artifact special cases vision.md rules out) |

**Not a delta:** the CommonJS `export=` binding (ff4757d3c). The binder calls `bindCommonJSTypeExports` from the same place in both versions. Most of the "7.1" comments in `src/` describe tsgo against tsc 6, not 7.0 against 7.1, and need nothing.

**Totals:** about 75 local, about 15 medium, 3 structural, and about 8 that can't be emulated.

## The structural ones
1. **Libs.** 7.0 needs its own lib directory (108 files) next to the 7.1 one, plus a 7.0 `lib_names.c` and `lib_features.c`. 7.0's files include `esnext.array`, `esnext.collection`, `esnext.error` and `esnext.typedarrays`, and lack `es2026.*`, `esnext.iterator`, `esnext.promise` and `esnext.modulesource`. The default target also changes. The generators (`lib_names.py`, `lib_features.py`) can be rerun on the v7.0.2 tree, so it's mechanical, but it's a second dataset with its own lookup path.
2. **Optional `undefined` members.** In 7.0 their declaration depends on which candidate a checker met first, so the output depends on checker history. tttt emulated this program-wide until today's 3baaad420. A 7.0 mode would restore those ~380 lines (`undefined_choice()`, the `UndefinedProps` table, `recheck_stale_undefined_props()`) under the flag. That's easy now and gets harder as `members.c` changes.
3. **Test infrastructure.** The 7.0.2 suite has a different layout:
   - Its tests are the TypeScript submodule's `tests/cases` at 4d4f005c, the same commit as tttt's TypeScript6 corpus, plus 317 tsgo-only tests.
   - Their baselines live under `reference/submodule/`.
   - The harness needs that second layout and a filter for the `TS-1` noise lines.
   - The `test/oracle` tools (sweep, `$ExpectType`, probes, mutate) import `github.com/microsoft/TypeScript/tsc/internal/...`; 7.0.2 is still `github.com/microsoft/typescript-go/internal`, with older APIs, so they need porting. 7.0.2's tree also has no `tspath.PathKey` or `contentmappertest`.

## Verification plan and cost
- **Second oracle:**
  - **CLI-level oracles** (corpora, clidiff, scenarios): the npm 7.0.2 binary is enough, and it works today.
  - **`tttt-oracle`, `$ExpectType` and probes:** these need a source build of v7.0.2: a second checkout through `ensure-typescript.sh` and porting `test/oracle`. About 1 to 1.5 days.
- **Suite:** the 7.0.2 baselines are already checked in upstream. The harness changes take about 1 day, and the suite's saved results file (`test/baseline_results.ts`) needs a second copy, one per mode.
- **Caches:** they are small (a few MB each). How much to regenerate:
  - **Corpora:** a full second copy, about 5 minutes.
  - **DefinitelyTyped:** a sample is enough given 0 of 1,075 differ.
  - **Source sweep:** a sampled second copy run in a sandbox, because incremental configs write `.tsbuildinfo` into the shared checkouts. That's why I didn't run it.
  - **`$ExpectType`, flagflip, dtsdiff:** samples.
  - In total about 1 hour of machine time once, plus triage.
- **Ongoing:** every later checker change must pass both modes. That doubles the gate time (suite 25 s to 50 s, `oracles.py --quick` about 2 to 4 minutes), and the two versions drift further apart as 7.1 moves to 7.2.

## Estimates
- **(a) One build-time flag for everything:** about 20 to 30 worker-days.
  - The ~75 local changes are 8 to 10 days.
  - The ~15 medium ones are 6 to 8 days.
  - The structural ones and the tooling are 4 to 5 days.
  - Regenerating caches and triaging new failures is 3 to 5 days.
  - With 3 or 4 workers that's about 1.5 weeks. About 60 suite configurations would still be unreachable: the 58 with the `TS-1` noise, plus the history-dependent cases.
  - A runtime `--ts-version` switch would cost about the same, since the libs are already data on disk.
- **(b) Only the cases users commonly hit:** about 4 to 6 worker-days.
  - It covers the exit code, the lib set and default target, distributed type parameters off, source phase imports and ambient import attributes rejected the 7.0 way, TS2589 in place of TS5114/TS5115, and the `--version` and typesVersions string.
  - Verification would be the npm 7.0.2 binary on the corpora and the DefinitelyTyped sample, plus about 50 suite tests.
  - About 250 suite configurations would still differ (edge semantics and type printing).
- **(c) Ship 7.1-beta behaviour and document the differences:** about half a day to turn this table into a page. On real code the differences that matter are four: the exit code (1 under 7.0.2), the stricter distributed-type-parameter check (drizzle, effect, vue, probably vscode), webpack's 14 TS2565 (a 7.0 bug) and the beta's TS5115 runaways.

My recommendation is (c). If someone has a concrete need, (b)'s exit-code and lib items are the cheap, visible part; the rest is mostly copying 7.0's bugs.

## What I couldn't determine
- The PRs behind TS18116, TS7080 and the TS5114 move.
- The exact cause of vscode's 10.
- The source sweep difference.
- How many of the 333 differing configurations tttt would fail in a 7.0 mode (there's no harness for that yet).
- 7.0's exact parser recovery for `import source` and `declare module "x" with {...}`.

Everything is in /tmp/claude-1001/-home-ai-tttt/b700f62f-8e15-4a53-8a74-ae83accd59a2/scratchpad/ts70-research/:
- `summ.txt`: the per-test difference buckets
- `difftests.txt`: the 333 differing configurations
- `run70/` and `dt70/`: the 7.0.2 output for the corpora and the DefinitelyTyped sample
- `commits.txt`: the commit list
- `blog.txt`: the beta announcement