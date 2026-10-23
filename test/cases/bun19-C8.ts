// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const key: string;
function f({ [key]: a = 1 }) {}
const sentinel: number = "";
