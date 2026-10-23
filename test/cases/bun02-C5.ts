// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
import { E1, E2 } from "./b";
/**
 * @throws {@link E1} when it fails
 * @returns {@link E2}
 */
export function f1() {}
const sentinel: number = ""; sentinel;
// @filename: b.ts
export class E1 extends Error {}
export class E2 {}
