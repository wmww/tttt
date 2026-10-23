// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type P17 = { [K in "a"]: 1, };
const sentinel: number = "";
