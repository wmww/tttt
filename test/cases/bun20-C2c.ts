// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
function g(): undefined {
    let x: string | undefined = undefined;
    while (cond()) {
        while (typeof x === "string" && cond()) {
            while (cond()) { x = "a"; }
        }
    }
    return x;
}
const sentinel: number = "";
