// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "paths": { "@/*": ["./*"] } }, "files": ["sub/a.ts"] }
// @filename: node_modules/dep/index.d.ts
import { T } from "./internal";
export declare function f(): T;
// @filename: node_modules/dep/internal.d.ts
export interface T { a: 1 }
// @filename: node_modules/dep/package.json
{ "exports": { ".": "./index.d.ts" } }
// @filename: sub/a.ts
import { f } from "dep";
export const x = f();
const sentinel: number = "";
// @filename: sub/package.json
{ "dependencies": { "dep": "*" } }
