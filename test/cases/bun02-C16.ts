// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { T1 } from "./b";
export const /** {@link T1} */ v1 = 1;
const sentinel: number = ""; sentinel;
// @filename: b.ts
export class T1 {}
