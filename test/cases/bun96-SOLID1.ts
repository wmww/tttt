// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type JSXElement = 1;
type { JSXElement };
const sentinel: number = "";
// @filename: b.ts
type { A } from "./a";
type ) ;
interface ] ;
namespace @ ;
module ~ 1;
type { A };
class C1 { type { } }
class C2 { interface? ) }
function f() { throw type { A }; }
