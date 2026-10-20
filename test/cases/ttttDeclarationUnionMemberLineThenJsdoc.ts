// @declaration: true
// @declarationMap: true
// @strict: true
// A reused union member's leading JSDoc on its own line after a `//` comment (ts-proto's oneof unions): tsc's
// writer indents lazily, so the comment lines and the member are indented like the line comment
export interface Value {
  kind:
    | //
    /** Represents a null value. */
    { a: 1 }
    | //
    /** Represents a double value. */
    { b: 2 }
    | undefined;
  k2:
    | /** A */ { a: 1 }
    | /** B */
    { b: 2 };
  k3:
    | // x
    { a: 1 }
    | { b: 2 };
  k4:
    /** A */
    { a: 1 };
  k5:
    | /** A */
    /** B */
    { a: 1 }
    | // c
    // d
    { b: 2 };
  k6: Array<
    // x
    /** y */
    { a: 1 }>;
}
export type T =
  | //
  /** doc */
  { a: 1 }
  | { b: 1 };
