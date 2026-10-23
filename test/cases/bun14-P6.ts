// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type X<K extends "a"> = { [P in "a"]: X<K> }[K];
const sentinel: number = "";
