// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
import type { type Nope } from "./c";
const sentinel: number = "";
// @filename: c.ts
export const c = 1;
