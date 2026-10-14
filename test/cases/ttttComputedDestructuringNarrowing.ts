// @strictNullChecks: true
// A computed key of a literal type names the destructured property for narrowing too (tsc's
// getDestructuringPropertyName through getLiteralTypeFromPropertyName): `w.window` narrowed by the assignment
{
    interface W { window?: W; a?: W }
    let w = {} as W;
    w.window = w;
    const { ["window"]: bar } = w;
    const { [(() => "window" as const)()]: { a: bar2 } } = w;
    const k = "window";
    const { [k]: { window: bar3 } } = w;
}
