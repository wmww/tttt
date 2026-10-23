// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace M { const x = 1; }
declare let m1: typeof M;
m1.toString;
declare const u: unknown;
const t1: typeof M | null | undefined = u;
const sentinel: number = "";
