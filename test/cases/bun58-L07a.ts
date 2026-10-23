// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { S as S1 } from "pkg/sub";
import { get } from "pkg";
import { S as S2 } from "other";
declare const s2: S2;
const s1: S1 = s2;
const s3: S2 = get();
const sentinel: number = "";
// @filename: node_modules/other/index.d.ts
export { S } from "pkg/sub";
// @filename: node_modules/other/node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/other/node_modules/pkg/sub/index.d.ts
export declare class S { private p: number; }
// @filename: node_modules/other/node_modules/pkg/sub/package.json
{ "name": "pkg-sub", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/other/package.json
{ "name": "other", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/pkg/index.d.ts
import { S } from "./sub/index"; export declare function get(): S;
// @filename: node_modules/pkg/package.json
{ "name": "pkg", "version": "1.0.0", "types": "index.d.ts" }
// @filename: node_modules/pkg/sub/index.d.ts
export declare class S { private p: number; }
// @filename: node_modules/pkg/sub/package.json
{ "name": "pkg-sub", "version": "1.0.0", "types": "index.d.ts" }
