// @declaration: true
// @strict: true
// A binding pattern's trailing comma is kept by declaration emit (tsc's NodeArray.hasTrailingComma), also with a
// comment after it (better-auth's `expiresIn = 1000 * 60 * 60 * 48, // Default expiration: 48 hours`)
export const f = ({
    a,
    b = 1, // why
}: { a: number; b?: number }) => 1;
export const g = ([
    a,
    b = 1, /* why */
]: [number, number?]) => 1;
export const h = ({ a, b /* no comma */ }: { a: number; b?: number }) => 1;
