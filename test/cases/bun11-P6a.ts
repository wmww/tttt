// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function h(v: string | number) { if (p(v)) { return v; } return 0; }
function p(x: string | number, { a } = { a: h(1) }) { return !!x; }
const sentinel: number = "";
