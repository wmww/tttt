// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() {
  import { A } from 123;
}
{
  import * as B from 123;
  import C from 123;
  import type { D } from 123;
}
namespace N {
  import { E } from 123;
}
const sentinel: number = "";
export {};
