// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { which as f } from "./node_modules/foo";
import { which as g } from "./node_modules/goo";
const x: "goo" = g;
const sentinel: number = "";
// @filename: node_modules/foo.d.ts
export declare const which: "foo";
// @filename: node_modules/goo.d.ts
export declare const which: "goo";
// @filename: node_modules/package.json
{"name":"r","version":"1.0.0"}
