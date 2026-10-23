// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const t1: 1 = null! as <T extends import("./m1").X>(x: T) => void;
const sentinel: number = "";
// @filename: m1.ts
export interface X { a: 1 }
