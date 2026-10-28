// An escaped reserved word is that word to tsc's scanner (`\u0065lse` is ElseKeyword): no identifier, no expression
// or statement starts with it (TS1135, TS1128); an escaped `static` before a block is a static block (TS1260), and an
// escaped modifier's TS1260 is dropped where the `;` expected before it is reported, a conflict marker after it or not
declare function f(...x: any[]): void;
f(\u0065lse);
\u0065lse;
class C { a = 1 st\u0061tic
<<<<<<< x
b = 2 }
class D { st\u0061tic {} }
