// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const dec: any; class A {} class B {}
@dec export default @dec class<> extends A, B {}
namespace N { export default class<> extends A, B {} }
const sentinel: number = "";
