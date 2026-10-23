// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { mk } from "outer";
export const v = mk();
const sentinel: number = "";
// @filename: node_modules/outer/index.d.ts
import { T } from "Binner";
import { T as T2 } from "ainner";
export declare function mk(): T;
// @filename: node_modules/outer/node_modules/Binner/index.d.ts
export interface T { t: 1 }
// @filename: node_modules/outer/node_modules/Binner/package.json
{ "name": "inner", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/outer/node_modules/ainner/index.d.ts
export interface T { t: 1 }
// @filename: node_modules/outer/node_modules/ainner/package.json
{ "name": "inner", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/outer/package.json
{ "name": "outer", "version": "1.0.0", "types": "index.d.ts" }
