// @strict: true
// A destructuring assignment's target is checked as an object literal (its grammar: TS1162, TS1042, TS1171) only where tsc
// asks for its type, as the contextual type of a right side that wants one (seed 20261115, privateWriteOnlyAccessorRead
// add-optional); `({ a?: x } = o)` is nothing (ttttObjectLiteralModifierAfterDuplicate, bun28-C12)
declare function g(): any;
let x: any, o: any;
({ public x } = { x: 1 });
({ [g(), "a"]: x } = { a: 1 });
({ a?: x } = ({ a: 1 }));
({ a?: x } = () => 1);
[{ b?: x }] = [{ b: 1 }];
({ a?: x } = o ? { a: 1 } : o);
({ a?: x } = `s`);
({ a?: x } = o);
