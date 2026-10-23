// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "experimentalDecorators": true } }
// @filename: a.ts
declare function d2(target: any, key: number): any;
class C { @d2 1n() {} }
const sentinel: number = "";
