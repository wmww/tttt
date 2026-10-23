// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import { a, b } from "./m";
export { a as aa };
b;
const sentinel: number = "";
// @filename: b.ts
import { b } from "./a";
// @filename: m.ts
const o = { a: 1, b: 2 }; export = o;
