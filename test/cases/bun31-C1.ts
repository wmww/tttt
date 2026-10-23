// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function m(a?: { f(): boolean }) { let t; if (t = a?.f()) a.f; }
const sentinel: number = "";
