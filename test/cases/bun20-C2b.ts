// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function cond(): boolean;
function h() {
    let x: string | number | boolean = "";
    do {
        x;
        try { x = 1; while (!x) { if (cond()) break; } }
        finally { do { if (cond()) break; } while (typeof x !== "boolean"); x.toFixed(); }
    } while (cond());
}
const sentinel: number = "";
