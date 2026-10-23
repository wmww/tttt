// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function run<T>(f: () => T): T;
function k6(ns: string[]) {
    let last: string | null = null;
    for (const n of ns) {
        if (n === last) continue;
        last = run(() => n);
    }
}
function k1(n: number, more: () => boolean) {
    let acc: number | null = null;
    while (more()) {
        if (n === acc) break;
        acc = [1].map(() => n)[0];
    }
}
async function k21(y: "a" | "b", more: () => boolean) {
    let x: "a" | "b" = "a";
    while (more()) {
        if (y === x) { }
        x = await Promise.resolve().then(() => y);
    }
}
// the two agree: no initializer, a declared type that is no union, an annotated callback
function a1(ns: string[]) { let last: string | undefined; for (const n of ns) { if (n === last) continue; last = run(() => n); } }
function a2(ns: string[]) { let last = ""; for (const n of ns) { if (n === last) continue; last = run(() => n); } }
function a3(ns: string[]) { let last: string | null = null; for (const n of ns) { if (n === last) continue; last = run((): string => n); } }
const sentinel: number = "";
