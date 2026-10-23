// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o = { x() { return 1 }, get x() { return "" }, set x(v) {} };
const r: bigint = o.x;
const sentinel: number = "";
