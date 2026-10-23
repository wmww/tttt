// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["w*d/a.ts"] }
// @filename: w*d/a.ts
import { x } from "pkg/x";
import { x as x2 } from "pkg/dir/x.d.ts";
import { y } from "#i/y";
const sentinel: number = "";
// @filename: w*d/node_modules/pkg/lib/x.d.ts
export const x: 1;
// @filename: w*d/node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "exports": { "./*": "./lib/*.d.ts", "./dir/": "./lib/" } }
// @filename: w*d/package.json
{ "name": "self", "imports": { "#i/*": "./src/*.ts" } }
// @filename: w*d/src/y.ts
export const y = 1;
