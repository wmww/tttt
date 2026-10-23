// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const k1: (...args: readonly [number, ...string[]]) => void = (a, ...r) => { r.push(""); };
const sentinel: number = "";
