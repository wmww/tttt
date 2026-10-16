// @strict: true
// `keyof` a mapped type with an `as` clause is the union of the names its keys map to (tsc's
// getIndexTypeForMappedType), not the keys of its expanded members: a name that comes out the error type
// (`T[k] extends X ? never : k` with `T[k]` the type of an unresolved import) makes the whole union any, where the
// members only have a string index signature for it. zod v4's input type of a shape with a schema imported from an
// unresolved module, through expect-type's toEqualTypeOf (nextra, TS2344)
import { thing } from "nowhere";
type Shape = { a: typeof thing; b: string; c: number; d: any; u: Unresolved };
type Required_<T> = { [k in keyof T as T[k] extends number ? never : k]: T[k] };
type Optional_<T> = { [k in keyof T as T[k] extends number ? k : never]?: T[k] };
const k1: { zz: 1 } = null! as [keyof Required_<Shape>];
const k2: { zz: 1 } = null! as [keyof Optional_<Shape>];
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type Input = Prettify<Required_<Shape> & Optional_<Shape>>;
const k3: { zz: 1 } = null! as [keyof Input];
type Strings = { [k in "x" | "y" as `get${Capitalize<k>}`]: k } & { [k in keyof Shape as string]: 1 };
const k4: { zz: 1 } = null! as [keyof Strings];
type Getters = { [k in keyof Shape as `get${Capitalize<k>}`]: 1 };
const k5: { zz: 1 } = null! as [keyof Getters];
