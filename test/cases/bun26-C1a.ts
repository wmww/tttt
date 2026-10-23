// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F<T, U> = T extends string ? T : { next: F<U | "x", 1> };
const r: F<0, string>["next"] = "abc";
const sentinel: number = "";
