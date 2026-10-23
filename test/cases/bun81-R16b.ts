// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare function apply<T, U>(x: T, f: (v: T) => U): U;
function sum(xs: number[]) {
    let acc: number | null = null;
    for (const n of xs) {
        acc = apply(acc, v => (v ?? 0) + n);
    }
    return acc;
}
function text(more: () => boolean) {
    let x: string | number = 0;
    while (more()) {
        x = apply(x, v => String(v));
    }
}
// the two agree: a method of an array, an annotated parameter, explicit type arguments
function a1(xs: number[]) { let acc: number | null = null; for (const n of xs) { acc = [acc].map(v => (v ?? 0) + n)[0]; } }
function a2(xs: number[]) { let acc: number | null = null; for (const n of xs) { acc = apply(acc, (v: number | null) => (v ?? 0) + n); } }
function a3(xs: number[]) { let acc: number | null = null; for (const n of xs) { acc = apply<number | null, number>(acc, v => (v ?? 0) + n); } }
const sentinel: number = "";
