// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "resolveJsonModule": true, "module": "esnext", "moduleResolution": "bundler" } }
// @filename: a.ts
import j from "./e.json";
const m1: Record<string, unknown> = j;
declare function values<T>(r: Record<string, T>): T[];
function f2(x: unknown, c: Function) {
    if (x instanceof c) {
        const m2: Record<string, unknown> = x;
        const m3: { [k: string]: number } = x;
        const m4: Record<string, any> = x;
        values(x);
    }
}
function f5(x: { a: 1 } | string, c: Function) {
    if (x instanceof c) {
        const m5: Record<string, unknown> = x;
    }
}
const { a, ...rest } = { a: 1 };
const m6: Record<string, unknown> = rest;
const sentinel: number = "";
// @filename: e.json
