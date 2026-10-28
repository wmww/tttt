// @noUnusedLocals: true
// @noUnusedParameters: true
// @noEmit: true
// tsgo's reportUnused skips a declaration whose subtree holds a node finished right after a parse error
// (ThisNodeHasError, propagated by the binder as ThisNodeOrAnySubNodesHasError): the switch's `let z` after the
// skipped `?:`, the class whose `}` is missing (its type parameter U), the method whose parameter is `1`, the variable
// whose initializer has an error. Known failing (agents/bugs/bun_others.md, the unusedSwitchStatement item)
switch (1) {
    default?:
        let z = 2;
}
var E = class {
    method(a, 1) {
    }
}
function f() {
    let x = (1 +);
    let y = 1 2;
}
class C<_T, U> {
    public m<_V, W>() { }
