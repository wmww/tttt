// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function fail(): never;
function t7(x: string | number | boolean) { fail(); switch (typeof x) { case "string": case "number": const a: null = x; } }
const sentinel: number = "";
