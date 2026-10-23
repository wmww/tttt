// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "importHelpers": true, "target": "es2022", "useDefineForClassFields": false, "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
export {};
declare const key: string; declare const dec: any; declare function use(...a: any[]): void;
export @dec class P { static y = 1; [key] = class { static x = 1 } }
const sentinel: number = "";
// @filename: b.ts
export {};
declare const key: string; declare const dec: any; declare function use(...a: any[]): void;
export default @dec class P { static y = 1; [key] = class { static x = 1 } }
const sentinel: number = "";
// @filename: node_modules/tslib/index.d.ts
export declare function __nothing(): void;
// @filename: node_modules/tslib/package.json
{ "name": "tslib", "version": "2.0.0", "types": "index.d.ts" }
