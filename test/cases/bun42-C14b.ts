// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "declaration": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { a, b } from "outer";
export default { x: b(), y: a() };
const sentinel: number = "";
// @filename: node_modules/outer/index.d.ts
import { B } from "inner";
import { A } from "inner/zz/deep";
export declare function b(): B;
export declare function a(): A;
// @filename: node_modules/outer/node_modules/inner/index.d.ts
export interface B { b: 1 }
// @filename: node_modules/outer/node_modules/inner/package.json
{ "name": "inner", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/outer/node_modules/inner/zz/deep.d.ts
export interface A { a: 1 }
// @filename: node_modules/outer/package.json
{ "name": "outer", "version": "1.0.0", "types": "index.d.ts" }
