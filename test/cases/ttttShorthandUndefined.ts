// A shorthand property `{ undefined }` takes the global undefined (tsc's resolveName finds undefinedSymbol): no
// TS18004. openclaw's stream-payload-utils.test.ts `{ undefined, null: null, same: payload, replacement }[kind]`
const kind = "undefined" as "undefined" | "x";
const expected = { undefined, x: 1 }[kind];
const o = { undefined };
declare let obj: { undefined: number };
({ undefined } = obj);
