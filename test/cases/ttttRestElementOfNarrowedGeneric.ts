// @strict: true
// The rest element of a pattern initialized with a generic reference a discriminant narrowed to never: tsc types the
// initializer again for the rest (CheckMode.RestBindingElement) without the pattern as contextual type, so the
// reference stays `T` (no constraint substituted, nothing narrowed) and the rest is `Omit<T, "kind">`, not TS2700.
// (The narrowingDestructuring literal-type mutant)
function f<T extends { kind: "a"; a: string } | { kind: "b"; b: number }>(t: T) {
    if (t.kind === "c" as string && t.kind !== "a" && t.kind !== "b") {
        const { kind, ...r1 } = t;
        r1;
    }
}
