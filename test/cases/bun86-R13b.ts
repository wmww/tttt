// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
Foob;
export {};
const sentinel: number = "";
// @filename: u.d.ts
export as namespace Foob;
export = nothing;
