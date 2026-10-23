// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { Sub } from "old/sub";
import { sub } from "dep";
sub(new Sub());
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { Sub } from "old/sub";
export declare function sub(c: Sub): void;
// @filename: node_modules/dep/node_modules/old/package.json
{ "name": "old", "version": "2.0.0" }
// @filename: node_modules/dep/node_modules/old/sub/index.d.ts
export declare class Sub { private p; }
// @filename: node_modules/dep/node_modules/old/sub/package.json
{ "name": "sub", "version": "1.0.0" }
// @filename: node_modules/old/package.json
{ "name": "old", "version": "1.0.0" }
// @filename: node_modules/old/sub/index.d.ts
export declare class Sub { private p; }
// @filename: node_modules/old/sub/package.json
{ "name": "sub", "version": "1.0.0" }
