// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const c: { [k: string]: (a: number) => void } = { "#a@1": a => a.length };
const sentinel: number = "";
