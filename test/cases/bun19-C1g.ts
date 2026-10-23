// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K<T> { x = 1; p!: T extends string ? typeof this.x : number; }
function g<T>(k: K<T>, n: number) { k.p = n; }
const sentinel: number = "";
