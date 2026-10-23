// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {}; declare const x: number; declare module "./plain" { export = x; }
const sentinel: number = "";
// @filename: plain.ts
export const a = 1;
