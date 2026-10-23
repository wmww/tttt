// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare let onu: { n: number } | undefined;
const r5: null = (({}) = onu);
const sentinel: number = "";
