// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type S<X> = [Extract<keyof X, number>, Extract<keyof X, string>];
declare const g3: { readonly 7: 1; readonly "8": 1; 9: 1; 10: 1 };
const { 9: _c, ...r3 } = g3;
const g1 = { get 7() { return 1 }, set 8(v: number) {}, 9: 1, 10: 1 };
const { 9: _a, ...r1 } = g1;
class G2 { readonly 7 = 1; 9 = 1; 10 = 1 }
const { 9: _b, ...r2 } = new G2();
const t1: S<typeof r1> = ["?", -99], t2: S<typeof r2> = ["?", -99], t3: S<typeof r3> = ["?", -99];
type M = { [K in keyof typeof r3]: K }; const m: M = 0;
const sentinel: number = "";
