// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function p2(x: string, c: boolean) { if (typeof x === "string") throw 1; return c ? true : false; }
const q2: number = p2;
const sentinel: number = "";
