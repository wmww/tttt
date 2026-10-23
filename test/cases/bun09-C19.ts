// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function rf([...ra, rb]) {}
const trf: never = rf;
const sentinel: number = "";
