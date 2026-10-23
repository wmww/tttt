// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
declare function id<T>(x: T): T;
declare const m: { a: "b"; b: "a" };
function f() {
    let x: "a" | "b" = "a", y: "a" | "b" = "a";
    do {
        if (y === x) { }
        if (cond()) { y = id(x); }
        if (cond()) continue;
        x = m[y];
    } while (cond());
}
const sentinel: number = "";
