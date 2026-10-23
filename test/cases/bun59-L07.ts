// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "esnext", "moduleResolution": "bundler", "typeRoots": ["modroot"] }, "files": ["a.ts"] }
// @filename: a.ts
import css from "sub/./x.css";
import { j } from "sub/./j";
import { t } from "sub/./t";
import { d } from "sub/./d";
import { d as d2 } from "sub/d";
import css2 from "sub/x.css";
const sentinel: number = "";
// @filename: modroot/sub/d.d.ts
export const d: 1;
// @filename: modroot/sub/j.js
export const j = 1;
// @filename: modroot/sub/t.tsx
export const t = 1;
// @filename: modroot/sub/x.d.css.ts
declare const css: string; export default css;
