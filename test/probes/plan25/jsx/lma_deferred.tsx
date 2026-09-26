// @jsx: preserve
// @strict: true
// A conditional type whose check type is an object/function type mentioning an outer type parameter (not itself a
// generic object type) must still resolve: tsc only defers when the check type isGenericObjectType/IndexType, and
// otherwise takes the false branch when the check fails under the permissive instantiation (type params -> any).
// tttt defers whenever a failed check mentions any type variable (ty_conditional_resolve's HACK), so
// LibraryManagedAttributes<Cell, P> for a component typed with the enclosing component's `Entry` stays unresolved.
// Reduced from bulletproof-react's table.tsx `{Cell ? <Cell entry={entry} /> : ...}`.
interface Memo<T> { $$memo: T }
interface Lazy<T> { $$lazy: T }
declare namespace JSX {
    interface Element { el: true }
    interface IntrinsicAttributes { key?: string }
    type LibraryManagedAttributes<C, P> = C extends Memo<infer T> | Lazy<infer T> ? P : C extends { defaultProps: infer D } ? P & D : P;
}
type Column<Entry> = { Cell?({ entry }: { entry: Entry }): JSX.Element };
function Table<Entry>(data: Entry[], columns: Column<Entry>[]) {
    return data.map(entry => columns.map(({ Cell }) => Cell ? <Cell entry={entry} /> : null));
}

// Without JSX
type L1<C, P> = C extends Memo<infer T> ? 1 : P;
type L2<C, P> = C extends { defaultProps: infer D } ? 1 : P;
function f<E>(e: E) {
    const a: L1<(x: E) => void, { e: E }> = { e };
    const b: L2<(x: E) => void, { e: E }> = { e };
    const c: L1<{ x: E }, { e: E }> = { e };
}
