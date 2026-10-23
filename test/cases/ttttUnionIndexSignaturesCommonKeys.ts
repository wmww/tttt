// @strict: true
// @noEmit: true
// A union none of whose members has the key as a property of its own reads the union's index signatures: the key
// types every member has, each member's signature for that key (tsc's getUnionIndexInfos), not each member's
// applicable one. pothos's ShapeFromEnumValues: `Values[K]['value']` with Values narrowed to the map branch of
// `Map | readonly string[]` was a spurious TS2536
interface C { value?: number | string }
type M = Record<string, C>;
type X4 = (M | (string[] & M))[string | number | symbol]['value'];
type X8 = (M | (string[] & M))[number];
type S1<V extends M | readonly string[]> = V extends M ? { [K in keyof V]: V[K]['value'] }[keyof V] : never;
type S3<V extends M | string[]> = V extends M ? V[keyof V]['value'] : never;
declare const u: M | (string[] & M);
const e = u[0];
