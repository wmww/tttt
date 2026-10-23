// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": ["foo"], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler"}}
// @filename: a.ts
import { make } from "dep";
declare module "foo" { interface C { added: number } }
make().own;
make().added;
const sentinel: number = "";
// @filename: node_modules/@types/foo/index.d.ts
export declare class C { own: number; }
// @filename: node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/dep/index.d.ts
import { C } from "foo";
export declare function make(): C;
// @filename: node_modules/dep/node_modules/@types/foo/index.d.ts
export declare class C { own: number; }
// @filename: node_modules/dep/node_modules/@types/foo/package.json
{ "name": "@types/foo", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/dep/package.json
{ "name": "dep", "version": "1.0.0", "types": "index.d.ts" }
