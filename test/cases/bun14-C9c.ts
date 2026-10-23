// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace M { const x = 1; }
declare let m1: typeof M;
m1.toString;
declare let m2: typeof M & string;
const e4: number = m2;
const sentinel: number = "";
