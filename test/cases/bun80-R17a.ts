// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "noUnusedLocals": true } }
// @filename: a.ts
import { X } from "./m";
/** Sets things up, see {@link X}. */
(function () {})();
export const sentinel: number = "";
// @filename: b.ts
import { X } from "./m";
/** {@link X} */
(globalThis as any).foo = 1;
export {};
// @filename: m.ts
export class X {}
