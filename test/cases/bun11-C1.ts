// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface Thenable { then(cb: number): void }
declare const th: Thenable;
async function* w6() { yield th; }
const sentinel: number = "";
