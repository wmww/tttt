// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type I<T> = (({ k: T | "b"; z: 1 } & { k: "a" }) | { z: 3 })["z"];
const i1: I<"c"> = 1;
const sentinel: number = "";
