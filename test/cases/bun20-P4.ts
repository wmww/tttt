// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare let h: ReturnType<typeof k>;
function k(p: () => void) { h(); return p; }
const sentinel: number = "";
