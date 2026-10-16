// @strict: true
// An array to a readonly array infers by type argument without going on the inference walk's stack, like two
// references to one generic type (tsc's inferFromTypes: isArrayType of both). On the stack, the array literal to
// `ReadonlyArray<F extends true ? Col<F> : {}>` made a nested pair of deferred `ReadonlyArray<Item<F>>` references
// look deeply nested, and the walk stopped there: F was inferred from `features` alone (tanstack-table's
// rowModelSlots.test.ts, TS2322)
interface Core { core: 1 }
interface Feat { feat: 2 }
type Item<F> = "auto" | { id: string; fn: (ctx: { f: F }) => F };
type Option<F> = Item<F> | ReadonlyArray<Item<F>>;
interface Col<F> { option?: Option<F> }
declare function construct<F>(o: { features: F; columns: ReadonlyArray<F extends true ? Col<F> : {}> }): F;
declare const option: Extract<Option<Feat>, ReadonlyArray<any>>;
declare const featCore: Core & Feat;
const r: number = construct({ features: featCore, columns: [{ option }] });
