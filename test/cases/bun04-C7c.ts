// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { f?: (n: number) => number };
const p = (o?.f)(1);
const sentinel: number = "";
