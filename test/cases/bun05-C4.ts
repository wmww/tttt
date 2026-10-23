// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const sy: symbol;
declare function tag2(s: TemplateStringsArray, x: number): void;
tag2`${sy}`;
const sentinel: number = "";
