// @strict: true
// A type member's modifier before a line break is one (tsc's isTypeMemberStart: what can end the member follows), and
// its name is missing (TS1003, TS7008 for `(Missing)`); a computed name's missing expression is TS1109 and the member
// stays, named `[` (TS7008), with no TS1170: a missing identifier is an entity name. A computed property signature
// without a type is implicitly any too
declare const k: "x";
type X = { a: 1; static
 {}; b: 2 };
type Y = { a: 1; [ };
interface I { [k] }
