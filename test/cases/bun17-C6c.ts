// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class GA<T> { constructor(x: T) {} }
class GDA extends GA<{ a: 1 }> {}
class GDD extends GA<{ a: 1; d: 1 }> {}
let k: typeof GDA = GDD;
const sentinel: number = "";
