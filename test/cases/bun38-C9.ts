// @filename: tsconfig.json
{ "extends": ["./x.json", "./y.json"], "compilerOptions": { "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function implicitAny(x) { return x; }
export const sentinel: number = "";
// @filename: x.json
{ "compilerOptions": { "strict": false } }
// @filename: y.json
{ "extends": "./z.json" }
// @filename: z.json
{ "compilerOptions": { "strict": null } }
