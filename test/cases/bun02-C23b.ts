// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type W1 = { [in K: string]: 1 };
type W2 = { [out K: string]: 1 };
const sentinel: number = "";
