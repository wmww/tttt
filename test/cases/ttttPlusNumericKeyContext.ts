// @strict: true
// @noEmit: true

// tsc's binder names a `[+2]` member "+2", the name its contextual type is looked up by: neither the property `2` nor
// a numeric index signature types `x`; `[-2]` is "-2", and a `"+2"` property does
const p1: { 2: (x: number) => void } = { [+2]: x => {} };
const p2: { 2: (x: number) => void } = { [2]: x => {} };
const p3: { [k: number]: (x: number) => void } = { [+2]: x => {} };
const p4: { "-2": (x: number) => void } = { [-2]: x => {} };
const p5: { "+2": (x: number) => void } = { [+2]: x => {} };
const p6: { 2: (x: number) => void } = { [+(2)]: x => {} };
declare function f<T>(o: { 16: (x: number) => T }): T;
f({ [+0x10]: x => x });
f({ [0x10]: x => x });
const o = { [+2]: 1, [-3]: "a" };
