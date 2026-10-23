// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "noUnusedLocals": true } }
// @filename: a.ts
import { X } from "./m";
export const f =
  /** {@link X} */
  a => a;
export const sentinel: number = "";
// @filename: m.ts
export class X {}
