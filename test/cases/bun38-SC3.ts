// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "module": "node16", "declaration": true, "resolvePackageJsonExports": false } }
// @filename: a.ts
import { make } from "pkg"; export const x = make();
// @filename: b.ts
export const sentinel: number = "";
// @filename: node_modules/pkg/index.d.ts
import { Inner } from "./internal"; export declare function make(): Inner;
// @filename: node_modules/pkg/internal.d.ts
export interface Inner { a: number }
// @filename: node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "exports": { ".": { "types": "./index.d.ts" } } }
