// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: { fail(): never } | undefined;
function g(x: string | number) { if (typeof x === "string") { o?.fail(), 0; } x.toFixed(); }
const sentinel: number = "";
