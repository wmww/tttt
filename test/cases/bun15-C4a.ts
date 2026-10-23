// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const p1: { 2: (x: number) => void } = { [+2]: x => {} };
const sentinel: number = "";
