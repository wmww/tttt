// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "importHelpers": true, "target": "es2022", "useDefineForClassFields": false, "module": "esnext", "moduleResolution": "bundler" }, "files": ["b1.ts"] }
// @filename: a1.ts
import { late } from "./b1"; export const u = new late();
// @filename: b1.ts
import { u } from "./a1"; u;
export const first = class { static x = 1 }; export const late = class { static x = 2 };
const sentinel: number = "";
// @filename: node_modules/tslib/index.d.ts
export declare function __nothing(): void;
// @filename: node_modules/tslib/package.json
{ "name": "tslib", "version": "2.0.0", "types": "index.d.ts" }
