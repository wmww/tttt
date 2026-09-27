// @target: es2020
// A deferred conditional type relates through its default constraint, whose true branch has the `infer` type
// parameters instantiated with what was inferred: nothing, for a generic check type, so unknown (tsc's
// getInferredTrueTypeFromConditionalType). Awaited<T>'s true branch is then never and Awaited<T> relates to T
// (valibot's awaitAsync: dataset.value = await dataset.value)
function h1<T extends Promise<unknown>>(x: T, y: Awaited<T>) { x = y; }
function h3<T>(x: T, y: Awaited<T>) { x = y; }
type B<T> = T extends { then(onfulfilled: infer F): any; } ? F extends (value: infer V) => any ? V : never : T;
function h6<T extends Promise<unknown>>(x: T, y: B<T>) { x = y; }
async function h7<T extends Promise<unknown>>(o: { value: T }) { o.value = await o.value; }
type C<T> = T extends [infer H extends string] ? H : 0;
function h8<T>(x: C<T>) { const s: string | 0 = x; }
// `{ [P in K]: E }[X]` falls back to the mapped type indexed by X's constraint (tsc's PragmaPseudoMap)
type U2I<U> = (U extends any ? (k: U) => void : never) extends ((k: infer I) => void) ? I : never;
interface Specs { a: { x: 1 }; b: { y: 2 } }
type M = { [K in keyof Specs]: { args: U2I<Specs[K]> } };
function h9<K extends keyof M>(m: M[K]) { const all: M[keyof M] = m; }
