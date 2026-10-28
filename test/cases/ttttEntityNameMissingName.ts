// In an entity name a `.` right after a dot is a missing name (tsc's parseEntityName: TS1003 at it) and the name goes on;
// `...` is a token of its own, which ends the name
const a = { b: { c: 1 } };
let x: typeof a..b;
let y: typeof a...b;
