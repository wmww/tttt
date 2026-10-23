// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
class K1 { m<T>(x: T) { class L { v!: T } return new L(); } }
class K2 { m<T>(x: T) { class L { w!: T } return new L(); } }
let a = new K1().m(1); a = new K2().m(1);
const sentinel: number = "";
