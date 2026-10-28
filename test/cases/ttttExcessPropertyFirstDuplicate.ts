// @strict: true
// A name written twice in an object literal is one symbol whose valueDeclaration is the first: an excess property
// is reported there (TS2353), the duplicate itself is TS1117
const o: { a?: 1 } = { b: 1, a: 1, b: 2 };
const p: { a?: 1 } = { a: 1, "c": 1, c: 2 };
