// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" }, "files": ["b.ts"] }
// @filename: a.ts
import * as ns from "./b";
import * as ns2 from "./b";
export const a2 = ns.foo2;
export const a = ns2.foo;
const sentinel: number = "";
// @filename: b.ts
import { a, a2 } from "./a";
const o = { foo2: a2, foo: a };
export = o;
