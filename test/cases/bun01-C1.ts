// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
export {};
import x1 = E1.a;
enum E1 { a }
import x2 = C2.t;
class C2 {}
namespace C2 { export const t = 1; }
const sentinel: number = "";
