// A mapped type's member ends at a `;`, a `}` or a newline, not a `,` (tsc's parseMappedType: parseSemicolon)
type A = { [K in "a"]: 1; b: 2 };
type B = { [K in "a"]: 1, };
type C = { [K in "a"]: 1, b: 2 };
type D = { [K in "a"]: 1
, };
const x: number = "";
