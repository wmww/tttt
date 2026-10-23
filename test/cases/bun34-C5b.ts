// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const dec: any;
class G { @dec public [k: string]: any }
const sentinel: number = "";
