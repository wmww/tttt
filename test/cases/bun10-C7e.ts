// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function memoize<T>(f: T): T;
function count(n: number) { if (n > 0) return cached(n - 1); return 0; }
const cached = memoize(count);
const sentinel: number = "";
