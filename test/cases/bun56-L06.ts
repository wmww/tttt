// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"], "jsx": "preserve" } }
// @filename: lt1.tsx
class A</T> {}
// @filename: lt2.tsx
function f</T>() {}
// @filename: lt3.tsx
type X</T> = 1
// @filename: lt4.tsx
interface I { </T>(): void }
// @filename: lt5.tsx
let x: Array</number>
// @filename: lt6.tsx
let x: [</a]
// @filename: lt7.tsx
function f(</a) {}
// @filename: lt8.tsx
class C { m</T>() {} }
