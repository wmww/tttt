// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler" }, "files": ["a.ts"] }
// @filename: a.ts
import * as m1 from "./node_modules/loose";
import * as m2 from "./node_modules/x/node_modules/moose";
const s1: 0 = m1.v;
const s2: 0 = m2.v;
const sentinel: number = "";
// @filename: node_modules/loose.d.ts
export const v: 'loose';
// @filename: node_modules/package.json
{ "name": "nm-root", "version": "4.0.0" }
// @filename: node_modules/x/node_modules/moose.d.ts
export const v: 'moose';
// @filename: node_modules/x/node_modules/package.json
{ "name": "nm-root", "version": "4.0.0" }
