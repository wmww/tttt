// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
declare function show<T>(x: T): { show: T };
const t: null = show(k());
function k() { let x: string | null = ""; do { if (cond()) break; } while (x); if (x != null) throw 0; return x; }
const sentinel: number = "";
