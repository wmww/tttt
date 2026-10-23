// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
type S<X> = [Extract<keyof X, number>, Extract<keyof X, string>];
function f1() {} f1[0] = 1; f1["1"] = 2; f1["1.5"] = 3; f1["-1"] = 4; f1["a"] = 5;
const f3 = () => {}; f3[0] = 1; f3["1"] = 2;
const t1: S<typeof f1> = ["?", -99], t3: S<typeof f3> = ["?", -99];
type M = { [K in keyof typeof f1]: K }; const m: M = 0;
const sentinel: number = "";
