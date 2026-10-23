// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import { A } from "noname";
import { a } from "dep";
a(new A());
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { A } from "noname";
export declare function a(c: A): void;
// @filename: node_modules/dep/node_modules/noname/index.d.ts
export declare class A { private p; }
// @filename: node_modules/dep/node_modules/noname/package.json
{ "name": "", "version": "1.0.0" }
// @filename: node_modules/noname/index.d.ts
export declare class A { private p; }
// @filename: node_modules/noname/package.json
{ "name": "", "version": "1.0.0" }
