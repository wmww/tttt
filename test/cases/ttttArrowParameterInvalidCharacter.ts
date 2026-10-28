// @noEmit: true
// An invalid character in a parenthesized arrow function's first parameter: tsc's parseDelimitedList skips the token no
// list takes and the arrow stands, the missing initializer's and the list's errors deduplicated on the TS1127
// (bun 43-C14a); without `=>` it is a parenthesized expression
var s12 = (x = €) => x;
var s2 = (x = €, y) => x;
var s3 = (x = € 1) => x;
var s4 = (x = €);
var s5 = (€) => 1;
var s6 = (x €) => 1;
var s7 = (x = 1€) => x;
var s8 = (x = €€) => x;
