// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const o = { [/*c*/ "1"]: 1 };
const k: "1" = null! as keyof typeof o;
const sentinel: number = "";
