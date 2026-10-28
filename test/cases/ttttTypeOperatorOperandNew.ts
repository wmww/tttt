// A type operator's operand is no constructor type (tsc's parseTypeOperatorOrHigher): `keyof new () => Z` is a
// reference to a type named `new`, escaped or not; in an intersection it is one (TS1388)
let x1: keyof new () => Z;
let x3: readonly new () => Z[];
let x4: 1 & n\u0065w () => Z;
