// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o1 = { private get x() { return 1 }, set x(v) {} };
const sentinel: number = "";
