// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
let q7: import("./b").BFoo.<"a">;
const sentinel: number = "";
// @filename: b.ts
export interface BFoo<X extends string> { x: X }
