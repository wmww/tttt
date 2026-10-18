# Plan 84: program loading in tsgo's two phases (package redirects by DFS order)

Status: not started (written by the triage of 2026-10-18). Size: one session, maybe two. Priority: low-medium: today it
only shows on projects opened through a symlinked directory (macOS `/tmp` -> `/private/tmp` is one; so is every
sub-agent worktree whose `thirdparty/corpora` is a symlink), and as tttt keeping files tsgo drops.

## What tsgo does
`compiler/filesparser.go`: phase one loads every file reachable from the roots in parallel, resolutions and all; a
file's `parseTaskData.packageId` is the id of *any* resolution that reached it (the first to set it, ids agree in
practice). Phase two, `collectFiles`, walks the tasks depth-first from the roots in order: the first file visited with
a package id is that id's representative; a later file with the same id becomes a redirect (`redirectFilesByPath`,
`filesByPath[path] = representative`) and `continue`s **before its subtasks**, so what only a redirect imports is not
in the program at all. Resolutions through a package's `exports` or `imports` carry the scope's id too (resolver.go
`getPackageId(path, scope)`, so a package importing itself by name gives its own sources an id); relative ones none.

## What tttt does
`program_load_resolved()` (program.c) decides at the resolution: the first resolution with an id registers its file
(`files_by_package_id`); a later one landing on another file makes that file a redirect (`SourceFile.package_redirect`,
unchecked, imports shared by path text, `program_share_redirect_ref()`). Load order is DFS like tsgo's, but:
1. a file loaded id-less (a root, a relative import) and reached with the id only later is not the representative, even
   when it comes first in program order. The 2026-10-18 triage added the HACK `sources_by_package_id` (hacks.md,
   "Package deduplication"): an id-less source outside node_modules is *assumed* to get its package's id. Wrong when
   nothing self-references the file (tsc keeps both copies);
2. a redirect's subtree is loaded and stays in the program (tsgo never collects it);
3. the second copy keeps its own AST and binding (hacks.md, same entry).

## Repro
```
mkdir -p /tmp/symrepro/real/packages/pkg/{src,test} /tmp/symrepro/real/packages/other/src /tmp/symrepro/real/node_modules
cd /tmp/symrepro
echo '{ "name": "pkg", "version": "1.0.0", "type": "module", "exports": { ".": "./src/index.ts", "./*": "./src/*.ts" } }' > real/packages/pkg/package.json
echo 'export const a: number = 1;' > real/packages/pkg/src/a.ts
echo 'export * from "./a.js";' > real/packages/pkg/src/index.ts
echo '{ "name": "other", "version": "1.0.0", "type": "module", "exports": { "./*": "./src/*.ts" } }' > real/packages/other/package.json
echo 'import { a } from "pkg/a"; export const o = a;' > real/packages/other/src/o.ts
echo 'import { a } from "pkg/a"; import { o } from "other/o"; export const b: string = a; export const c: string = o;' > real/packages/pkg/test/t.ts
echo '{ "compilerOptions": { "module": "nodenext", "strict": true, "noEmit": true }, "include": ["src", "test"] }' > real/packages/pkg/tsconfig.json
ln -s ../packages/pkg real/node_modules/pkg; ln -s ../packages/other real/node_modules/other; ln -s real link
tsc -p link/packages/pkg/tsconfig.json --listFilesOnly   # one a.ts (link path), o.ts at its real path
```
Drop the `"pkg/a"` import from t.ts and tsc lists both `a.ts` copies (nothing reaches the link one with the id); tttt
with the HACK lists one. At scale: effect through a symlinked checkout (`cd` into a worktree whose corpora is a link,
`-p thirdparty/corpora/effect/packages/effect/tsconfig.corpus.json --checkers 1`): tsc 5 diagnostics from 5 kept
real-path copies (GlobalValue, HKT, Types, Utils, internal/errors: the files vitest's sources import by name that no
effect file self-references); tttt ~25 (TS5115s, TS2375; before the HACK and the exports package ids, 3165). The
harness can't express this (units can't sit under a symlinked directory: tried `@link: /real -> /alias` with a tsconfig
unit, tsgo baselines nothing).

## Approach
Split loading: (1) load and resolve everything reachable (what `program_add_made_source()` does today, minus the
redirect decisions), recording each file's first package id; (2) a DFS from the roots in tsgo's order (references,
then imports, as the loader already orders them) that builds `source_files_ordered`, marks redirects and drops the
files only redirects reached (they were parsed: drop their ASTs, unregister them; nothing is bound before
`program_resolve_names()`, check that). Then `files_by_package_id`, `sources_by_package_id` and the HACK go, and
`program_share_redirect_ref()` becomes "the redirect's refs are the representative's" by construction. Mind: include
reasons (`record_include_reason()` runs at load), `found_searching_node_modules`/`node_modules_js_depth`, the
deferred-body and checker-balance code that reads program order, `-b`'s per-project programs, and the load pool
(parallel parse) which must keep its determinism.

## Validation
The repro above both ways (with and without the self-reference) lists tsc's files; effect through a symlinked
checkout matches tsc's 5 diagnostics; `scripts/oracles.py` unchanged (sweep and source sweep have duplicated packages:
`sourcesweep_checker.md`'s base-ui extra vitest copy may move); instructions flat.
