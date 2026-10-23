// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { b } from "./p1";
b.nope;
const sentinel: number = "";
// @filename: p1.ts
import * as b from "./p2"; export { b }; export const av = 1;
// @filename: p2.ts
import * as a from "./p1"; export { a }; export const bv = 1;
