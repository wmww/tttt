// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function dec(...x: any[]): any;
@dec import("./m");
export @dec import("./m");
function f() { @dec import("./m"); }
const sentinel: number = "";
// @filename: m.ts
export {};
