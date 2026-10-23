// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
async function w2() { const f = (a = await) => { }; }
const sentinel: number = "";
