// @filename: tsconfig.json
{ "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"], "strict": false, "strict": null } }
// @filename: a.ts
function implicitAny(x) { return x; }
export const sentinel: number = "";
