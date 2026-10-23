// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(u: { kind: 0 } | { kind: 1; b: number }) { const { ...kind } = u; if (kind) u.b; }
const sentinel: number = "";
