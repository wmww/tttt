// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o = { x: 1, m(a: typeof this) { return a.x }, n(a: typeof this.x) { return a } };
const q1: number = o;
const sentinel: number = "";
