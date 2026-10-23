// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022", "esnext.decorators"], "importHelpers": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
export {};
declare const dec: any;
@dec class Q { @dec get x(): number; @dec set x(v: number) {} }
const sentinel: number = "";
// @filename: node_modules/tslib/index.d.ts
export declare function __nothing(): void;
// @filename: node_modules/tslib/package.json
{ "name": "tslib", "version": "2.0.0", "types": "index.d.ts" }
