// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noUnusedLocals": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace A { export interface I { a: 1 } }
export namespace N { const A = 1; import foo = A; export let v: foo.I; }
export const sentinel: number = "";
