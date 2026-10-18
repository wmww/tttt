// @strict: true
// A property an optional spread property merged with prints as the object literal it was written as, when its
// properties match by name: tsgo's serializeTypeForDeclaration reuses the first declaration's syntax, and
// pseudoTypeEquivalentToType finds the type's properties by name (the merged type is the spread's `Loc`)
interface Loc { line: number; column: number }
interface Rec { name: string; flag: boolean; location: Loc }
function make(overrides: Partial<Rec> = {}): Rec {
    return {
        name: "x",
        location: { line: 1, column: 1 },
        ...overrides,
    };
}
declare const overrides: Partial<Rec>;
const r = { name: "x", location: { line: 1, column: 1 }, ...overrides };
const v: number = r.location;
const w: number = r;
