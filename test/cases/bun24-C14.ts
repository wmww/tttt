// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
declare const tlv: { a: 1 };
class Klass { meth<T>(x: T) { class L { v!: T } return new L(); } }
const v: 1 = { z: null! as "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", q: new Klass().meth(1), last: tlv };
const sentinel: number = "";
