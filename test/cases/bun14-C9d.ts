// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<T>(t: T) { return {}; }
const r = mk(1);
r.toString;
declare let x: typeof r & string;
const e: number = x;
const sentinel: number = "";
