// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "noEmit": true, "module": "esnext", "moduleResolution": "bundler", "types": [], "lib": ["es2022"] }, "files": ["a.ts"] }
// @filename: a.ts
import { K, viaImportType, viaNamespace } from "outer";
export const a = viaImportType();
export const b = viaNamespace();
export const c = new K().m;
export class D { m(x: typeof import("outer/node_modules/inner")) { return x; } }
// @filename: node_modules/outer/package.json
{ "name": "outer", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/outer/index.d.ts
import * as inner from "inner";
export declare function viaImportType(): typeof import("inner");
export declare function viaNamespace(): typeof inner;
export declare class K { m(x: import("inner").Inner): typeof import("inner"); }
// @filename: node_modules/outer/node_modules/inner/package.json
{ "name": "inner", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/outer/node_modules/inner/index.d.ts
export interface Inner { a: number }
export declare const value: number;
