// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "module": "nodenext" } }
// @filename: a.ts
import { sub } from "pkg//sub";
const sentinel: number = "";
// @filename: package.json
{ "name": "pkg", "exports": { "./sub": "./sub.ts" } }
// @filename: sub.ts
export const sub = 2;
