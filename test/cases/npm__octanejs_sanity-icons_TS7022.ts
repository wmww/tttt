// origin: npm @octanejs/sanity-icons@0.1.16 https://github.com/octanejs/octane packages/sanity-icons/tsconfig.json seed 20261201
// flip: yes
// @strict: true
// @noEmit: true
// @lib: esnext, dom
// @target: esnext
// From octane's src/dom-focused-move.ts: a do-while loop reassigning a declared `Element | undefined` from a const
// initialized by a call on it. tsgo resolves `root` without a circularity; tttt reports TS7022 on it (the project's
// only error: a flip). It needs the `contains(candidate)` read earlier in the loop body; without it both agree.
export function f(node: Node, focused: Element | null): boolean {
	if (focused !== null && node.nodeType === 1) {
		let candidate: Element | undefined = focused;
		do {
			if ((node as Element).contains(candidate)) return true;
			const root = candidate.getRootNode();
			candidate = root.nodeType === 11 ? (root as ShadowRoot).host : undefined;
		} while (candidate !== undefined);
	}
	return false;
}
