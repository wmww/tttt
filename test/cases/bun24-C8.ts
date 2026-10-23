// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class G<T> { static readonly s: unique symbol; }
const t: { (a: { [G.s]: 1 }): void; (q: 1): 1 } = {};
const sentinel: number = "";
