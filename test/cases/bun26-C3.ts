// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f({ a = 1 }) {}
const i: { [k: string]: number } = null! as Parameters<typeof f>[0];
const sentinel: number = "";
