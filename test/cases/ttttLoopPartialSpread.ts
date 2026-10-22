// @noImplicitAny: true
// An auto-typed variable spread into its own value in a loop: TS2488 for its partial type undefined and for its
// final one (mutation of selfReferencingSpreadInLoop)

declare function foo(): number;
let additional = undefined;
for (const subcomponent of [1, 2, 3]) {
    additional = [...additional, foo()];
}
