// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type F1<T> = ([T]) extends ([string]) ? Uppercase<T> : never;
const sentinel: number = "";
