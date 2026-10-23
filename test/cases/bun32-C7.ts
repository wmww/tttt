// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "exactOptionalPropertyTypes": true } }
// @filename: a.ts
function f(o: { a?: string }) { if (o.hasOwnProperty(("a"))) o.a.length; }
function g(o: { a?: string }) { if ((o.hasOwnProperty)("a")) o.a.length; }
const sentinel: number = "";
