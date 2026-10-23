// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export const o = { get a() { return 1 }, a: 2 };
o.a = 3;
const sentinel: number = "";
