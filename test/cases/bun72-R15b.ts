// @filename: tsconfig.json
{ "compilerOptions": { "strict": true, "noEmit": true, "types": [], "lib": ["es2022"] } }
// @filename: a.ts
function f1(a: number, b = g1()) { return a; }
function g1() { return f1("wrong", 1); }
function g2() { return f2("wrong", 1); }
function f2(a: number, b = g2()) { return a; }
function f3(a: number, b = g3()) { return a; }
function g3() { return f3(1, 1); }
function f4(a: number, b = g4()) { return a; }
function g4() { return [f4("wrong", [])]; }
function f5(a: number, b = g5()) { return a; }
function g5() { return { r: f5("wrong") }; }
function f6(a: number, b = g6()) { return a; }
function g6() { return { r: f6(1) }; }
function f7(a: number, b: ReturnType<typeof g7>) { return a; }
function g7() { return { r: f7("wrong", null!) }; }
function f8(a: number, b: ReturnType<typeof g8>) { return a; }
function g8() { return { r: f8(1, null!) }; }
function f9(a: number, b = g9().r) { return [a]; }
function g9() { return { r: f9("wrong", []) }; }
class C10 { m(a: number, b = this.n()) { return [a]; } n() { return { r: this.m("wrong", null!) }; } }
class C11 { m(a: number, b = this.n()) { return [a]; } n() { return { r: this.m(1, null!) }; } }
class C12 { p = this.m("wrong", null!); m(a: number, b = this.p) { return [a]; } }
class C13 { p = this.m(1, null!); m(a: number, b = this.p) { return [a]; } }
const o14 = { m(a: number, b = o14.n()) { return [a]; }, n() { return { r: o14.m("wrong", null!) }; } };
function f15(a: number, b = g15(), c = 1) { return [a]; }
function g15() { return { r: f15("wrong", null!, "also wrong") }; }
function f16(a: number, b: string, c = g16()) { return [a]; }
function g16() { return { r: f16(1, 2, null!) }; }
function f17(a: number, b: string, c = g17()) { return [a]; }
function g17() { return { r: f17(1, "", null!) }; }
function f18(cb: (x: number) => void, b = g18()) { return [cb]; }
function g18() { return { r: f18((x: string) => {}, null!) }; }
function f19(cb: (x: number) => void, b = g19()) { return [cb]; }
function g19() { return { r: f19(x => { x.nope }, null!) }; }
const sentinel: number = "";
