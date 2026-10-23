// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
declare const m: { a: "b"; b: "a" };
function g() {
    let x: "a" | "b" = "a", y: "a" | "b" = "a";
    do {
        y = x;
        switch (x) { case y: const p: "a" = x; break; default: x = m[y]; }
    } while (cond());
}
const sentinel: number = "";
