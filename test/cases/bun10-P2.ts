// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const a = i["z"];
declare const i: I;
declare const s: string;
interface I { [k: typeof k2]: typeof x1 }
const x1 = a;
const k2 = a ? s : s;
const sentinel: number = "";
