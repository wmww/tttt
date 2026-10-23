// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function d9(x: [number, string] | string) { if (!(x instanceof Array)) { x.toUpperCase(); } }
const sentinel: number = "";
