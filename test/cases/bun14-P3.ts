// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function mk<T>(t: T) { return {}; }
const r = mk(1);
declare const u: unknown;
const t1: typeof r | null | undefined = u;
const sentinel: number = "";
