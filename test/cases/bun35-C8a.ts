// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "typeRoots": ["./types"], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import { C as C1 } from "modroot";
import { C as C2 } from "modroot/";
export const c: C1 = new C2();
const sentinel: number = "";
// @filename: types/modroot/index.d.ts
export declare class C { private p; }
