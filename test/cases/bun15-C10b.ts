// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f5<T extends undefined>(a: { x?: T }) { const s2 = { x: 2, ...a }; const n2: number = s2.x; }
const sentinel: number = "";
