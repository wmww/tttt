// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Sw<T, U> = T extends string ? [T, U] : Sw<U | "k", T>;
const r: ["a", 1] | ["k", 1] = null! as Sw<1, "a">;
const sentinel: number = "";
