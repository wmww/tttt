// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noImplicitAny": false, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const p7 = { set x([a]) {} };
p7.x = 1;
const sentinel: number = "";
