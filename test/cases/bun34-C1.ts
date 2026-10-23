// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "noUnusedLocals": true } }
// @filename: a.ts
class E {}
/** @throws {@link E} */
export function f() {}
const sentinel: number = ""; sentinel;
