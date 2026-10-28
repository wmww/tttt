// @strict: true
// An arithmetic compound assignment's left operand that is only null or undefined is TS18048/TS18049 and the error
// type from then on, its target too (tsc's checkNonNullType before checkAssignmentOperator): no TS2322. `+=` with a
// string operand isn't non-null checked, and is TS2322
declare let u: undefined;
u **= 2;
u += 1;
u -= 1;
declare let nn: null | undefined;
nn *= 3;
declare let un: number | undefined;
un *= 3;
declare let us: undefined;
us += "a";
let num = undefined;
num **= BigInt(2);
