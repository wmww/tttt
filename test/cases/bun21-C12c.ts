// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function g(r: { 0: string } & Iterable<number>) { let [a]: Iterable<string | number> & { 0: string | number } = r; a.length; }
const sentinel: number = "";
