// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f({ cb = () => {} } = { cb: () => {} }) {}
const sentinel: number = "";
