// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "k";
const x: 0 = which;
const sentinel: number = "";
// @filename: node_modules/k/index.d.ts
export declare const which: "index";
// @filename: node_modules/k/package.json
{"types":"t.d.ts","types":42}
// @filename: node_modules/k/t.d.ts
export declare const which: "t";
