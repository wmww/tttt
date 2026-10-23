// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
namespace A { export enum E { z = 0, o = 1 } }
namespace B { export enum E { z = -0, o = 1 } }
let x: A.E = null! as B.E;
const sentinel: number = "";
