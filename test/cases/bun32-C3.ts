// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f(o: { k: "a"; a: 1 } | { k: "b" } | undefined) { if ((o!).k === "a") o!.a; }
const sentinel: number = "";
