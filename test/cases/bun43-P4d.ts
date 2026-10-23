// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { Foo } from "./p";
/** a /** {@link Foo} */
export const sentinel: number = "";
// @filename: p.ts
export class Foo {}
