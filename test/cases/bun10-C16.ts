// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class G<T> { x!: T; constructor(...args: any[]) {} }
function f1<T extends typeof G>(base: T) { class D extends base { } }
const sentinel: number = "";
