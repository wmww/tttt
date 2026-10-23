// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const anyC: any;
class A7 extends anyC<A7["own"]> { own = 1 }
const sentinel: number = "";
