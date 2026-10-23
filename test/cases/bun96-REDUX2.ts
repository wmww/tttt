// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Store<S, T> { getState(): S & T }
interface Pair<A, B> { either: A | B }
type AB = Store<{ a: 1 }, { b: 2 }>
type BA = Store<{ b: 2 }, { a: 1 }>

// two references to one generic type are related by variances, not by their members
function r1<E extends {}>(s: AB & E) { const t: BA = s }
function r2<E extends object>(s: AB & E): BA { return s }
function r3<E extends {}>(s: Pair<1, 2> & E) { const t: Pair<2, 1> = s }
// both tools agree on these
function r4(s: AB) { const t: BA = s }
function r5<E>(s: AB & E) { const t: BA = s }
function r6<E extends { c: 3 }>(s: AB & E) { const t: BA = s }
function r7(s: AB & {}) { const t: BA = s }
const sentinel: number = "";
