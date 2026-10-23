// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "commonjs", "moduleResolution": "bundler", "resolvePackageJsonExports": false, "resolvePackageJsonImports": false }, "files": ["a.ts"] }
// @filename: a.ts
import { which } from "self";
export const i: "esm" = which;
export const sentinel: number = "";
// @filename: cjs.d.ts
export declare const which: "cjs";
// @filename: esm.d.ts
export declare const which: "esm";
// @filename: package.json
{ "name": "self", "exports": { "import": "./esm.d.ts", "require": "./cjs.d.ts" } }
