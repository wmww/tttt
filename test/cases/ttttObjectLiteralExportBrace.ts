// In a member list `export {` is a name and a brace, not a modifier (tsc's canFollowExportModifier takes no `{` or
// `*`): an object literal left open before `export {};` reads `export` as a property, ':' expected (seed 20261114,
// thislessFunctionsNotContextSensitive3 delete-stmt)
declare function f(x: { a?: number }): void;
f({
  a: 1,

const b = f({
  a: 2,
});

export {};
