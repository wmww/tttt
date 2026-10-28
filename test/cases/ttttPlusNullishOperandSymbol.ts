// @target: es2015
// A `+` operand null or undefined is TS18047/TS18048, then the error type: the other operand is still checked, TS2469 for a symbol
let symbol!: symbol;
let str = undefined;
str += symbol;
let s2 = null;
s2 += symbol;
let s3;
s3 += symbol;
declare let a: any;
a += symbol;
declare let n: number | undefined;
n += 1;
let u = undefined;
u + 1;
null + undefined;
