# Root files in include-pattern order expose an order-dependent union reduction (vue)

tsc's matchFiles puts each file into the bucket of the first include pattern that matches it and returns the buckets
in include order (utilities.ts matchFiles, `results[includeIndex]`); tttt's collect_files() keeps walk order. The port
is written (`collect_files()`'s buckets) but pushes everything into bucket 0 (a HACK marker there), because with tsc's
order (verified identical to tsc's `fileNames` for vue's tsconfig.corpus.json, 464 roots) vue gets a spurious

    packages/runtime-dom/src/directives/vModel.ts:446 TS2345 Argument of type 'DirectiveBinding<any, string, any>'
    is not assignable to parameter of type 'DirectiveBinding<any, "number", any>'.

`modelToUse` is `resolveDynamicModel(...)`'s inferred return type, a union of ModelDirective<..., 'number'>,
ModelDirective<..., 'trim'|'number'|'lazy'>, ModelDirective<HTMLInputElement> and so on, subtype-reduced; which
members survive (and so the parameter type of `getSSRProps` on the union) depends on what was created first.
Variances of ObjectDirective / DirectiveBinding are tsc's ([4,4,1,4] modulo #0, and [1, invariant+unreliable, 1]),
so the difference is in the reduction or the union call's signature. Reduce: vue's runtime-core directives.ts +
runtime-dom vModel.ts in a project, roots ordered like the corpus, compare `modelToUse`'s type with tsc's
(`tsctypeat.js`). Once fixed, push to `buckets[i]` and rerun every oracle (root order moves type creation order).
