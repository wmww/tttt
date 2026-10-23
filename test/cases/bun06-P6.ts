// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function* z3() { const f = ({ yield }) => { }; }
async function w3() { const f = ({ await }) => { }; }
const sentinel: number = "";
