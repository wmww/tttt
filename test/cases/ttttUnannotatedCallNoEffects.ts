// @strict: true
// A call statement to an unannotated function whose body returns no value has no effects (tsc's
// getTypePredicateFromBody infers a predicate only from a single `return` of a value): deciding so doesn't type its
// parameters, which may need what is being inferred. openclaw's session-canonical-key.test.ts, `seed(scope, entry)` in
// createScope with seed's `scope: ReturnType<typeof createScope>`
declare function save(x: { a: string; key: string }, e: unknown): void;
function createScope(entry?: unknown, key = "k") {
    const scope = { a: "main", key };
    if (entry) {
        seed(scope, entry);
    }
    return scope;
}
function seed(scope: ReturnType<typeof createScope>, entry: unknown, k = scope.key) {
    save({ ...scope, key: k }, entry);
}
