# Plan 03: a real-world corpus (tsc's own compiler sources)
Type: execution, 1-2 sessions. Written by the insights session after the type core migration.

## Why
Every session so far steered by the tsc baseline tests: ~13k tiny files, each poking one feature. Nobody had run tttt on
a real program. `./realworld.py` does (entry `thirdparty/tsc/src/compiler/checker.ts`, which pulls in all of
`src/compiler`: 146k lines, 7.8 MB, **zero errors under tsc**). Result at b751872:

- **1119 syntax errors in 31 files, then the process dies with glibc `mremap_chunk(): invalid pointer`** (heap
  corruption) 0.7 s in, before name resolution. The checker has never seen real code.
- The syntax shapes are *not* exotic, they're just rare in the test suite, which is why `failstats.py --syntax` shows
  them as a "flat tail" and the previous insights session judged a third parser session not worth it. On real code
  they're fatal: regex literals (`/^".+"$/`, by far the largest), optional call `o.f?.()`, `if (a) b; else c;`
  (**`else` after a braceless `if` branch**), the comma operator (`return pos += 3, token = X;`), array destructuring
  with rest (`const [a, ...rest] = f()`), optional/named tuple members (`[kind?: K, ...rest: T[]]`), type predicates
  (`x is T` - parsed as something, see triage), parameter defaults that are `|` expressions, object literals with many
  `a: b.c,` lines (TS1005 ':' at checker.ts:52850 - find out what that is).
- The bench corpus (`bench/make_corpus.py`) is passing tests replicated inside namespaces; vision.md has called it
  weak since day one. This replaces it.

## Do
1. `./realworld.py` until it reports **0 TS1xxx and exit without a signal**. Work from its "by token" table; rerun
   often, it takes a second. Fix the parser properly (a regex literal is lexed where an expression may start, the way
   tsc rescans `/`; don't special-case "after `=`"). Each construct must also be traversed by name resolution, the
   typecheck walk and `baseline_test_traversal.c`, as the parser-gaps sessions did. Check `./fasttest.sh --summary`
   for what each fix does to the suite (expect gains from `?.()`, regex, destructuring: those are in triage's
   "remaining syntax gaps" list, delete what you fix from there).
2. Find the heap corruption first if it survives a debug build: `valgrind ./debug/tttt -q thirdparty/tsc/src/compiler/scanner.ts`
   (or whichever small file reproduces it; `semver.ts` and `path.ts` have many errors and few imports). A parser that
   corrupts memory on bad input is a bug in its own right, not something the syntax fixes should be allowed to hide:
   fix the cause, not the input.
3. Once it parses: record what happens next in `measurements.md` - does name resolution finish, does typecheck
   finish, how long does each phase take, peak RSS, how many diagnostics by code (`./realworld.py` prints all that),
   and the same numbers for `tsc --noEmit` and `tsgo` on the same entry (see `bench.sh` for how they're invoked; a
   `tsconfig` is in `thirdparty/tsc/src/compiler/`). Runaways (time/memory) in bind or check: bisect to a file,
   write up in `agents/bugs/`, contain if cheap. **Do not chase the checker diagnostics in this plan** - there will
   be thousands (no narrowing, no module resolution subtleties, ...). The count by code is the deliverable; it is
   the project's second oracle from now on and later sessions drive it down.
4. Add the corpus to `bench.sh` as a second, pinned benchmark (same files every session, unlike the test-derived one,
   so numbers are comparable across sessions). Keep the old one until the new one runs to completion.
5. If parse time or RSS per source byte on this corpus look bad next to tsgo (RSS was ~120 bytes per source byte on
   the synthetic corpus; AST nodes are malloc'd one by one, `DEBUG_ALLOCATOR` is on in release), don't fix it here:
   measure (callgrind works, see triage), write the numbers down, and leave a triage note for the perf planning
   session in `todo.md`.

## Done when
`./realworld.py` shows no syntax errors and no crash; `measurements.md` has the real-corpus numbers for tttt/tsc/tsgo
and the diagnostics-by-code table; suite pass counts didn't drop (`./fasttest.sh --summary`, save with `-s`);
valgrind clean on a sample of the touched parser families. If one session isn't enough, leave the remaining token
table in this file and let `todo.md` schedule a second.
