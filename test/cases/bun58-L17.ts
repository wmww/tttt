// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o = 1; const v = "";
namespace N { export { v as o }; const t: boolean = o; }
namespace M { const w = true; export { w as o }; const t: null = o; function f() { const u: null = o; } }
namespace P { export { v as o }; export namespace Q { const t: null = o; } }
const sentinel: number = "";
