// `?.` is one token before anything but a digit: `a?..b` is `a?.` missing its name, which ends the member expression
// (the parenthesized expression misses its `)`, `.b` follows it); `?.<T>` before a template is a tagged template with
// type arguments in the chain, TS1358
declare const a: any, b: any;
type T = 1;
(a?..b);
a?.<T>`x`;
