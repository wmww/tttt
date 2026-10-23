// @noEmit: true

// A reference to a generic alias whose instantiation is the error type (a tuple too large to represent, TS2799)
// has its type arguments' constraints left unchecked (tsc's checkTypeReferenceOrImport: no TS2344 for "3")
type BuildTuple<L extends number, T extends any[] = [any]> =
    T['length'] extends L ? T : BuildTuple<L, [...T, ...T]>;

type A = BuildTuple<"3">;

// Constraints are still checked where the instantiation is fine
type Id<N extends number> = [N];
type B = Id<"3">;
