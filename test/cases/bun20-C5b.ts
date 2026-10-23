// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class Q { v: string | undefined; isS(): this is { v: string } { return true; } }
function b22(q: Q) { if ((q.isS)()) { q.v.length; } }
const sentinel: number = "";
