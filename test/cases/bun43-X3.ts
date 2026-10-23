// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { Foo } from "./p";
export const /** {@link Foo} */ x = 1;
export const sentinel: number = "";
// @filename: p.ts
export interface Foo {}
