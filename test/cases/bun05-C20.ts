// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function set<T extends Record<string, unknown>, K extends keyof T>(o: T, k: K) { o[k] = 1; }
const sentinel: number = "";
