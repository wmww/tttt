// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
function w(): string {
    let x: string | boolean | undefined = "";
    while (cond()) {
        while (typeof x !== "boolean") {
            if (cond()) continue;
            if (cond()) break;
            x = undefined;
        }
    }
    return x;
}
const sentinel: number = "";
