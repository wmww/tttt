// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function ng(x: number): void;
const e1 = ng<>;
type Q1 = typeof ng<>;
const sentinel: number = "";
