// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type as = number;
type satisfies<T> = T;
namespace satisfies { export const y = 1; }
namespace N { interface as { a: number } }
function f() { type as = 1; let x: as = 1; return x; }
const sentinel: number = "";
