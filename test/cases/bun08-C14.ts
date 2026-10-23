// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import d1 from "./e1"; d1.length; d1.a;
const sentinel: number = "";
// @filename: e1.d.ts
declare const v: { default: string; __esModule: true; a: number }; export = v;
