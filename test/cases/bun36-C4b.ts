// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "e";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/e/index.d.ts
export declare const which: "index";
// @filename: node_modules/e/package.json
{"name":"e","version":"1.0.0","exports":{".":{"types@>= 4.0":"./t.d.ts","types":"./index.d.ts"}}}
// @filename: node_modules/e/t.d.ts
export declare const which: "t";
