// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
declare const m: { a: "b"; b: "a" };
function f() {
    let x: "a" | "b" = "a", y: "a" | "b" = "a";
    while (cond()) {
        if (x !== y) { x = m[y]; }
        switch (x) { case "b": break; }
    }
}
const sentinel: number = "";
