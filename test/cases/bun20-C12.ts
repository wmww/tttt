// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const nv: never;
function t6(x: "a" | "b" | "c") {
    switch (x) { case "a": break; case nv: const a: null = x; break; case "b": break; }
}
const sentinel: number = "";
