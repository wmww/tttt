// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type E<T> = { [K in keyof T]: E<T[]> };
const sentinel: number = "";
