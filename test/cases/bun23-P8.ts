// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class G<T> { static readonly u: unique symbol = Symbol(); g!: T }
namespace A { export class X<Q> { a!: Q } }
namespace B { export class X<Q> { b!: Q } }
const s: { (p: [A.X<{ [K in typeof G.u]: 1 }>, B.X<1>]): void } = {};
const sentinel: number = "";
