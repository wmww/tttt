// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<U, T extends { a: (x: U) => void }>(v: T["a"]): U;
f((x: string) => {});
const sentinel: number = "";
