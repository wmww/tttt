// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { Foo } from "./p";
/** {@link Foo—x} */
export const sentinel: number = "";
// @filename: p.ts
export class Foo {}
