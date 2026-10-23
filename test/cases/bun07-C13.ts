// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
declare const oa: { a: number; b: string };
({ a: 1, a: 2, ...oa });
const sentinel: number = "";
