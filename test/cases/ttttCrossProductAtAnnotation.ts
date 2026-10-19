// TS2590 of an intersection's cross product written in a declaration's annotation goes to the type node (tsc's
// checkUnionOrIntersectionType resolves it there), not to the declaration's name; a parameter's likewise
type U0 = { a0: 1 } | { b0: 1 } | { c0: 1 };
type U1 = { a1: 1 } | { b1: 1 } | { c1: 1 };
type U2 = { a2: 1 } | { b2: 1 } | { c2: 1 };
type U3 = { a3: 1 } | { b3: 1 } | { c3: 1 };
type U4 = { a4: 1 } | { b4: 1 } | { c4: 1 };
type U5 = { a5: 1 } | { b5: 1 } | { c5: 1 };
type U6 = { a6: 1 } | { b6: 1 } | { c6: 1 };
type U7 = { a7: 1 } | { b7: 1 } | { c7: 1 };
type U8 = { a8: 1 } | { b8: 1 } | { c8: 1 };
type U9 = { a9: 1 } | { b9: 1 } | { c9: 1 };
type U10 = { a10: 1 } | { b10: 1 } | { c10: 1 };
type U11 = { a11: 1 } | { b11: 1 } | { c11: 1 };
type U12 = { a12: 1 } | { b12: 1 } | { c12: 1 };
type U13 = { a13: 1 } | { b13: 1 } | { c13: 1 };
type U14 = { a14: 1 } | { b14: 1 } | { c14: 1 };
type U15 = { a15: 1 } | { b15: 1 } | { c15: 1 };
type U16 = { a16: 1 } | { b16: 1 } | { c16: 1 };
declare const x: U0 & U1 & U2 & U3 & U4 & U5 & U6 & U7 & U8 & U9 & U10 & U11 & U12 & U13 & U14 & U15 & U16;
let y: U0 & U1 & U2 & U3 & U4 & U5 & U6 & U7 & U8 & U9 & U10 & U11 & U12 & U13 & U14 & U15 & U16;
function f(p: U16 & U15 & U14 & U13 & U12 & U11 & U10 & U9 & U8 & U7 & U6 & U5 & U4 & U3 & U2 & U1 & U0) {}
class C { m(a: number, q?: U1 & U2 & U3 & U4 & U5 & U6 & U7 & U8 & U9 & U10 & U11 & U12 & U13 & U14 & U15 & U16 & U0) {} }
