// @strict: true
// @lib: es2023,dom
// The quick type of `f?.(x)` with a callee that isn't nullish and has one non-generic signature is its return type,
// the arguments unchecked (tsc's getQuickTypeOfExpression: getReturnTypeOfSingleNonGenericSignatureOfCallChain).
// Otherwise `y` needs `el` in the loop, whose back edge narrows by `element !== el`, whose type needs `target`, whose
// initializer checks `y` (openscience's create-auto-scroll.tsx: three spurious TS7022)
declare const store: { contentRef?: HTMLElement };
declare const doc: { at?(x: number, y: number): Element | null; pick(x: number, y: number): Element | null };
export function f(el: HTMLElement | undefined, bounds: DOMRect, x: number) {
  if (!el) return;
  for (const offset of [1, 2]) {
    const y = bounds.top + Math.min(offset, el.clientHeight / 2);
    const target = doc.pick?.(x, y);
    const element = target?.closest("p") ?? target;
    if (element !== el && store.contentRef?.contains(element)) {
    }
  }
}
// A nullish callee has no quick type: that one is circular in tsc too
export function g(el: HTMLElement | undefined, bounds: DOMRect, x: number) {
  if (!el) return;
  for (const offset of [1, 2]) {
    const y = bounds.top + Math.min(offset, el.clientHeight / 2);
    const target = doc.at?.(x, y);
    const element = target ?? undefined;
    if (element !== el && store.contentRef?.contains(element)) {
    }
  }
}
