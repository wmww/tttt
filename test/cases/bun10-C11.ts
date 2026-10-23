// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const ks: string;
class D1 { [ks] = 1; }
interface D1 { b: string; }
const sentinel: number = "";
