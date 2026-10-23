// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface I1 { get x(); }
const sentinel: number = "";
// @filename: b.ts
interface I1 { set x(v: string); }
