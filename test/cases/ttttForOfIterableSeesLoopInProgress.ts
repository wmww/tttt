// @strict: true
// @lib: es2022
// A for-of variable's iterable is checked without isolating the loops in progress (tsc's checkRightHandSideOfForOf:
// checkNonNullExpression, uncached): `node` at the while loop's back edge needs `sibling` (the `sibling === node`
// narrowing), whose iterable needs `node` at the while loop again, which answers its partial type. tttt isolated the
// for-of variable's evaluation like a declaration's and reported TS7022 at `sibling` (hermes-ide's useFocusTrap)
interface Coll { readonly length: number; [index: number]: Elem; }
interface Elem { readonly children: Coll; readonly parentElement: Elem | null; }
declare function from<T>(arrayLike: ArrayLike<T>): T[];
function f(root: Elem) {
  let node: Elem | null = root;
  while (node) {
    for (const sibling of from(node.children)) {
      if (sibling === node) continue;
    }
    node = node.parentElement;
  }
}
