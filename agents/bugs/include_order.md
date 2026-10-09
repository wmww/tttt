# vue's vModel.ts:446 TS2345 depends on the checker count (variance measurement order)

Root files come in tsgo's include-pattern order since 2026-10-09: tsgo's matchFileNames (vfs/vfsmatch) puts each file
into the result list of the first include pattern that matches it and flattens the lists in include order;
`collect_files()` does the same (the walk-order HACK that used to be there is gone). Walk order inside a pattern's list
is the sorted DFS, files before subdirectories, like tsgo's globVisitor.

tsgo 7.1 reports, at every `--checkers` value including 1 and `--singleThreaded`:

    packages/runtime-dom/src/directives/vModel.ts:446 TS2345 Argument of type 'DirectiveBinding<any, string, any>'
    is not assignable to parameter of type 'DirectiveBinding<any, "number", any>'.

and the vue corpus cache (`test/corpora.tsc.jsonl`) expects it. tttt reports it at `--checkers` 2 and 4 (the default,
so the corpus counts it) but not at 1 or 3.

Mechanism (2026-10-08, agent ta, against tsc 6, which was clean in its full check): `resolveDynamicModel`'s inferred
return type is subtype-reduced; whether `ModelDirective<HTMLSelectElement, 'number'>` is a strict subtype of
`ModelDirective<HTMLInputElement, string>` depends on ObjectDirective's HostElement variance, which is circular with
DirectiveHook/DirectiveBinding/SSRDirectiveHook. Measured while DirectiveHook's variances are in flight
(runtime-core directives.ts:184 first, Ternary.Unknown), HostElement comes out independent (4), Select is reduced
away, no error; measured from vModel.ts first, HostElement is contravariant (2) and the error appears. tttt's
`--checkers 1` order still measures from directives.ts first; tsgo's single checker doesn't, though its program file
order (filesparser.go collectFiles: imports before importers, as tsc 6) should be the same. What tsgo does differently
at one checker is not known: an instrumented tsgo (test/oracle overlay logging getVariancesWorker's start with the
current node) on vue at `--checkers 1` against `TTTT_DUMP_VARIANCES=1 ./release/tttt --checkers 1 -p
thirdparty/corpora/vue/tsconfig.corpus.json` would show which measurement starts first.

At K > 1 tsgo assigns files to checkers by weight and import adjacency (compiler/checkerpool.go
getCheckerAssociationsInOrder), tttt by contiguous runs of program order (`deal_files()`), so which checker sees
directives.ts before vModel.ts differs anyway; each checker measures variances on its own (hacks.md "Each checker
measures variances"). Porting tsgo's association policy would make per-checker results comparable at K > 1.

Reduction: a tsconfig extending vue's tsconfig.corpus.json with `"types": []` and `files` packages/global.d.ts and
runtime-dom/src/directives/vModel.ts.
