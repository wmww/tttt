// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var x: any;
class C extends Object, (delete x) {}
const sentinel: number = "";
