// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: p001.ts
interface I { \u0065xport z: 1 }
// @filename: p002.ts
interface I { \u0069n z: 1 }
// @filename: p003.ts
interface I { d\u0065fault class: 1 }
// @filename: p004.ts
interface I { readonly \u0065xport z: 1 }
// @filename: p005.ts
type T = { \u0065xport z: 1 }
// @filename: p006.ts
interface I { \u0065xport [k: string]: 1 }
// @filename: p007.ts
class C { \u0065xport z = 1 }
// @filename: p008.ts
class C { \u0063onst z = 1 }
// @filename: p009.ts
function f(\u0065xport z: number) {}
