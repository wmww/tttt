// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
const f = async <T extends object>(x: T)
// @filename: b.ts
const g = async <T,>(x: T)
// @filename: c.ts
async <T extends U>(x);
// @filename: d.ts
async <T = U>(x);
// @filename: e.ts
async <const T>(x);
