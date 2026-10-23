// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const o: any;
{ export = <Missing1>o }
{ export = missing2() }
function f() { export = missing3() }
{ export default missing4() }
function g() { export default <Missing5>o }
const sentinel: number = "";
