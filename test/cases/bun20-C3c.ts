// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
declare const m: { a: "b"; b: "c"; c: "d"; d: "a" };
function q() {
    let x: "a" | "b" | "c" | "d" = "a";
    while (cond()) {
        while (x !== "d") { x = m[x]; if (cond()) break; }
    }
}
const sentinel: number = "";
