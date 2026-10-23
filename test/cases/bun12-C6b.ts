// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Z3<T> = { [K in "a" as Z3<T>]: 1 };
const sentinel: number = "";
