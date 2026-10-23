// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.mts
import * as u1 from "./u1.cjs"; u1.a; u1.default.a;
const sentinel: number = "";
// @filename: u1.d.cts
declare const u: { a: number } | { a: string; b: number }; export = u;
