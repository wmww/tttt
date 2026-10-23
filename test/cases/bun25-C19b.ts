// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const p: never = null! as { "#a@1": 1 };
const sentinel: number = "";
