// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class C { static readonly "x-y": unique symbol = Symbol(); }
({})[C["x-y"]];
const sentinel: number = "";
