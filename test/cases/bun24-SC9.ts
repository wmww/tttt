// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f1(...a: number[], ...b: [string, number]): void;
const s1: string = f1;
const sentinel: number = "";
