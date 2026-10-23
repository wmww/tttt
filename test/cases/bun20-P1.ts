// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "exactOptionalPropertyTypes": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
interface H { a?: number; hasOwnProperty(k: string): this is H; }
function f(x: H) { if (x.hasOwnProperty("a")) { x.a.toFixed(); } }
const sentinel: number = "";
