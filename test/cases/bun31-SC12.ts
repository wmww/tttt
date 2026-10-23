// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "allowUnreachableCode": false } }
// @filename: a.ts
class R7 { get g() { switch ((() => { throw 1 })()) { case 1: break } } }
function r1() { switch ((() => { throw 1 })()) { case 1: break } class C { } }
function r3() { switch ((() => { throw 1 })()) { case 1: break } { let a; function a() { } } }
const sentinel: number = "";
