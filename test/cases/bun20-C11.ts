// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function t4<T>(x: unknown, t: T) {
    class C { v!: T }
    if (x instanceof C) { const a: string = x.v; }
}
const sentinel: number = "";
