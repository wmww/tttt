// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let n: never; declare const kn: number;
class C1 { [kn] = 1; [+1] = "s"; }
n = new C1()[5];
const sentinel: number = "";
