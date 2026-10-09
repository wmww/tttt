# Root files in include-pattern order expose a per-checker variance order dependence (vue)

tsc's matchFiles puts each file into the bucket of the first include pattern that matches it and returns the buckets
in include order (utilities.ts matchFiles, `results[includeIndex]`); tttt's collect_files() keeps walk order. The port
is one line (`collect_files()` pushes to `buckets[0]`, the HACK marker there: push to `buckets[i]` instead; verified
identical to tsc's `fileNames` for vue's tsconfig.corpus.json, 464 roots). With it vue is clean at `--checkers 1` but
reports one TS2345 at 2..4 checkers (the default):

    packages/runtime-dom/src/directives/vModel.ts:446 TS2345 Argument of type 'DirectiveBinding<any, string, any>'
    is not assignable to parameter of type 'DirectiveBinding<any, "number", any>'.

Mechanism (2026-10-08, agent ta): tsc itself is order dependent here. `resolveDynamicModel`'s inferred return type is
subtype-reduced; whether `ModelDirective<HTMLSelectElement, 'number'>` is a strict subtype of
`ModelDirective<HTMLInputElement, string>` depends on ObjectDirective's HostElement variance, which is circular with
DirectiveHook/DirectiveBinding/SSRDirectiveHook. In tsc's full check the first measurement is DirectiveHook's
(runtime-core directives.ts:184, `binding.dir[name] as DirectiveHook | ...` related to the imported bare
`DirectiveHook`): ObjectDirective is measured while DirectiveHook's variances are in flight (Ternary.Unknown), so its
HostElement comes out independent (4), Select is reduced away, the union call takes the `string` signature, no error.
Measured from elsewhere first (tsc's language service on vModel.ts alone: `scripts/tsctypeat.sh` shows two members)
HostElement is contravariant (2) and the error appears in tsc too.

tttt now measures in tsc's order at one checker (the bare-alias fix, alias_variance_applies() /
ty_alias_args_filled()). With K checkers the checker that gets vModel.ts never checked directives.ts, so it measures
from vModel.ts and gets tsc's language-service answer. Each checker measures variances on its own (hacks.md, "Each
checker measures variances"), so no file order fixes this at K > 1. Options: keep the HACK (walk order happens to put
directives.ts in vModel.ts's checker), or make variances a program-level, checker-independent result (a sequential
pre-pass in program order, or sharing measured variances between checkers deterministically): a plan of its own, worth
it only if more such cases show up (`scripts/checkerdiff.py` finds them).

Reduction: `/tmp`-style tsconfig extending vue's tsconfig.corpus.json with `"types": []`, `files`: packages/global.d.ts
and runtime-dom/src/directives/vModel.ts: tsc clean, tttt clean at `--checkers 1`, TS2345 at 2..4.

tsgo 7.0.2 (2026-10-08, `tsc` on PATH, vue's tsconfig.corpus.json, `--noEmit`): reports this TS2345 at vModel.ts:446
at `--checkers 1`, 2, 4 and `--singleThreaded` alike (10 errors in all, the other 9 in dts-test's
defineComponent.test-d.tsx), so tsgo's single-checker order already differs from tsc 6's full check here. On the
reduction above tsgo reports it at `--checkers 1` and 2 but not at 4: tsgo's per-checker caches are order dependent
too. For a TS7 retarget vue's corpus would expect the TS2345 (and tttt's walk-order HACK would then be the wrong way
round); which order tsgo's checkers measure in would need a look at its checker-assignment (likely file index modulo
the checker count, unlike tttt's contiguous runs).
