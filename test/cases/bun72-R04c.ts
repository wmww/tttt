// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "noResolve": true}, "files": ["a.ts", "node_modules/old/index.d.ts", "node_modules/dep/index.d.ts", "node_modules/dep/node_modules/old/index.d.ts"]}
// @filename: a.ts
import { C } from "old";
import { take } from "dep";
take(new C());
const sentinel: number = "";
// @filename: node_modules/dep/index.d.ts
import { C } from "old";
export declare function take(c: C): void;
// @filename: node_modules/dep/node_modules/old/index.d.ts
export declare class C { private p; }
// @filename: node_modules/dep/node_modules/old/package.json
{ "name": "old", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/dep/package.json
{ "name": "dep", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/old/index.d.ts
export declare class C { private p; }
// @filename: node_modules/old/package.json
{ "name": "old", "version": "1.0.0", "types": "index.d.ts" }
