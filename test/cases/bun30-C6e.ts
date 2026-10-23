// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare var x: any;
enum E { [delete x] = 1 }
const sentinel: number = "";
