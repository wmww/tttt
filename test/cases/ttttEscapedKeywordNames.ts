// Escaped keywords follow tsc's scanner and parser: a reserved word spelled with escapes is that word (TS1389 for
// `var v\u0061r`, TS1390 for a parameter, named by the keyword: bun 43-C2f), and a keyword consumed as an identifier
// (`declare glob\u0061l {}`, parseIdentifier's nextTokenWithoutCheck) is no TS1260 (bun 43-C2e)
// @filename: a.ts
var v\u0061r;
let \u0063lass = 1;
var \u0061wait2 = 1;
var \u{61}bc = 1;
function f(v\u0061r) {}
// @filename: b.ts
export {};
declare glob\u0061l { interface G {} }
let g: G;
