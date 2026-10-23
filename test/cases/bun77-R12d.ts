// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "useDefineForClassFields": false, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f() { return class { p = arguments; constructor(arguments: any) {} }; }
function g() { return class { p = arguments; }; }
class C1 { p = arguments; constructor(arguments: any) {} }
const sentinel: number = "";
