// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "traceResolution": true }, "files": ["a.ts"] }
// @filename: a.ts
import * as m16 from "./node_modules/loose";
const sentinel: number = "";
// @filename: node_modules/loose.d.ts
export const v: 1;
// @filename: node_modules/package.json
{ "name": "nm-root", "version": "4.0.0" }
