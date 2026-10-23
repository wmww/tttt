// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const c: boolean;
class G1 { get a() { if (c) { throw 1; return 1; } } }
const sentinel: number = "";
