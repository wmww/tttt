// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { x } from "./eqmissing";
const sentinel: number = "";
// @filename: eqmissing.ts
import x = require("./missing-mod2");
export = x;
