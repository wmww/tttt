// @strict: true
// findMostOverlappyType keys by literal types: a numeric literal name `{ 1: x }` is the number 1, which overlaps a
// number or string index signature and a numeric property `1:`, not a property declared `"1":` (and vice versa)
interface S { (a: string): number; }
interface A { 0: string; }
interface B { [a: number]: S; }
interface C { [a: string]: S; }
interface D { "1": S; }
interface E { 1: S; }
var x1: A | B = { "1": a => a };
var x2: A | C = { 1: a => a };
var x3: A | D = { 1: a => a };
var x4: A | E = { "1": a => a };
var x5: A | E = { 1: a => a };
