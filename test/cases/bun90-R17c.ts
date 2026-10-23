// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "importHelpers": true, "target": "es2022", "useDefineForClassFields": false, "module": "esnext", "moduleResolution": "bundler" }, "files": ["b5.ts"] }
// @filename: a5.ts
import { mk } from "./b5"; export const u = mk();
// @filename: b5.ts
import { u } from "./a5"; u;
const zero = class { static x = 0 };
export function mk() { const k = class { static x = 1 }; return new k(); }
const sentinel: number = "";
// @filename: node_modules/tslib/index.d.ts
export declare function __nothing(): void;
// @filename: node_modules/tslib/package.json
{ "name": "tslib", "version": "2.0.0", "types": "index.d.ts" }
