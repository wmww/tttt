ttttSpreadMergedPropertyPrintsLiteral.ts(7,5): error TS2322: Type '{ flag?: boolean | undefined; name: string; location: { line: number; column: number; }; }' is not assignable to type 'Rec'.
  Types of property 'flag' are incompatible.
    Type 'boolean | undefined' is not assignable to type 'boolean'.
      Type 'undefined' is not assignable to type 'boolean'.
ttttSpreadMergedPropertyPrintsLiteral.ts(15,7): error TS2322: Type 'Loc' is not assignable to type 'number'.
ttttSpreadMergedPropertyPrintsLiteral.ts(16,7): error TS2322: Type '{ flag?: boolean | undefined; name: string; location: { line: number; column: number; }; }' is not assignable to type 'number'.


==== ttttSpreadMergedPropertyPrintsLiteral.ts (3 errors) ====
    // A property an optional spread property merged with prints as the object literal it was written as, when its
    // properties match by name: tsgo's serializeTypeForDeclaration reuses the first declaration's syntax, and
    // pseudoTypeEquivalentToType finds the type's properties by name (the merged type is the spread's `Loc`)
    interface Loc { line: number; column: number }
    interface Rec { name: string; flag: boolean; location: Loc }
    function make(overrides: Partial<Rec> = {}): Rec {
        return {
        ~~~~~~
!!! error TS2322: Type '{ flag?: boolean | undefined; name: string; location: { line: number; column: number; }; }' is not assignable to type 'Rec'.
!!! error TS2322:   Types of property 'flag' are incompatible.
!!! error TS2322:     Type 'boolean | undefined' is not assignable to type 'boolean'.
!!! error TS2322:       Type 'undefined' is not assignable to type 'boolean'.
            name: "x",
            location: { line: 1, column: 1 },
            ...overrides,
        };
    }
    declare const overrides: Partial<Rec>;
    const r = { name: "x", location: { line: 1, column: 1 }, ...overrides };
    const v: number = r.location;
          ~
!!! error TS2322: Type 'Loc' is not assignable to type 'number'.
    const w: number = r;
          ~
!!! error TS2322: Type '{ flag?: boolean | undefined; name: string; location: { line: number; column: number; }; }' is not assignable to type 'number'.
    