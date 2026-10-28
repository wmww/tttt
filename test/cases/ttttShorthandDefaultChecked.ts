// A shorthand property's initializer outside a destructuring target (TS1312) is checked like any expression: tsc's
// checkShorthandPropertyAssignment. Here recovery makes `ExtendedItem = BaseItem & {...}` one: TS2363 and TS18050
type Query = { primary: { x: number } };
const response: Query = {
    primary: { x: 1 },
type
    ExtendedItem = BaseItem & {
        description: string | null
    },
};
const o = { n = 1 - "s" };
