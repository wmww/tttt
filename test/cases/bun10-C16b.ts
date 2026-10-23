// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class P { y = 1; constructor(...args: any[]) {} }
function f2<T extends typeof P>(base: T) { class D extends base<string> { } }
const sentinel: number = "";
