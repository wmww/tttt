// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "exactOptionalPropertyTypes": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function c1(x: { a?: number }) { if ((x.hasOwnProperty)("a")) { x.a.toFixed(); } }
const sentinel: number = "";
