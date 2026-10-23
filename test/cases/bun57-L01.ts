// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const a: any, b: any;
@a export export @b class C1 {}
@a public export @b class C2 {}
@a declare export @b class C3 {}
function f() { @a export @b class C4 {} }
namespace N { export @a default class C5 {} }
namespace M { @a export default @b class C6 {} }
const sentinel: number = "";
