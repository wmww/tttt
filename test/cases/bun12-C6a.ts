// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type Z8<T> = { [K in "a" as Z8<T[]>]: 1 };
const sentinel: number = "";
