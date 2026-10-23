// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class D { static readonly u: unique symbol = Symbol(); }
class E { static readonly u: unique symbol = Symbol(); }
let x1 = null! as { [K in typeof D.u]: 1 }; x1 = null! as { [K in typeof E.u]: 1 };
const sentinel: number = "";
