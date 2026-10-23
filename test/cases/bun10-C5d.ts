// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs" } }
// @filename: a.ts
import e = require("./e");
export const ye = e;
const sentinel: number = "";
// @filename: e.ts
import { ye } from "./a";
export = ye;
