// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f({ a = 1 }) {}
declare let p: Parameters<typeof f>[0];
const r: { [k: string]: number } = p;
const sentinel: number = "";
