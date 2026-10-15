// @declaration: true
// Property names: `#` alone isn't an identifier; an object literal's computed literal name is renamed by its text
// (tsgo's reuseName): an identifier, a non-negative number, else a string
export const tokens = { "#": 1, "a b": 2, x: 3 } as const;
export const langs = { ['da-DK']: { name: "d" }, ['x']: 1, [`y`]: 2 };
export class L { m() { return { ['_rolumns']: [] as never[] }; } }
const SYM = "meta";
export const o = { [SYM]: 1 };
export const nums = { ['0']: 1, [1e3]: 2, ['01']: 3, ['#x']: 4 };
