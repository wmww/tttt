// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function d2(target: any, context: { name: "1n" }): any;
class C { @d2 1n() {} }
const sentinel: number = "";
