// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
const r4 = { a = [1] } as const; const r4n: null = r4;
const sentinel: number = "";
