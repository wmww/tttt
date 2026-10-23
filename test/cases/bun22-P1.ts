// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function f<A extends B, B>(y: B, cb: (b: B) => void): A | B;
const k = "a";
k || f("b", b => { const t: "b" = b; });
const sentinel: number = "";
