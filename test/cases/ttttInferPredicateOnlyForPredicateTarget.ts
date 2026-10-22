// Inference from a signature asks for the source's type predicate only when the target has one (tsc's
// applyToReturnTypes). `Parameters<typeof renderHeader>` infers from renderHeader's signature to
// `(...args: infer P) => any`: asking for renderHeader's predicate evaluated its returns, which call headerElement,
// whose signature was being resolved: a spurious "Expected 1-0 arguments", TS7023 and TS2502 (dev-3.0, source sweep)
// @noTypesAndSymbols: true
// @strict: true
declare function render(x: string): { ok: boolean };
function renderHeader(route: string, n: number = 1, extra?: { a?: string }) {
	return render(headerElement(route, n, extra));
}
function headerElement(route: string, n: number = 2, extra?: Parameters<typeof renderHeader>[2]) {
	return route + n + (extra?.a ?? "");
}
renderHeader("x");
