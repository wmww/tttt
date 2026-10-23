// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function e1<T extends Missing>(t: T) { const n: never = t.foo; }
const sentinel: number = "";
