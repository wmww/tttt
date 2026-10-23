// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler"}, "files": ["a.ts"]}
// @filename: a.ts
import { f } from "dep";
export const x = f();
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { T } from "./internal";
export declare function f(): T;
// @filename: node_modules/dep/internal.d.ts
export interface T { a: 1 }
// @filename: node_modules/dep/package.json
{"name": "dep", "version": "1.0.0", "exports": {".": "./index.d.ts"}}
// @filename: package.json
{ "name": "p", "imports": { "#app/*": "./*" } }
