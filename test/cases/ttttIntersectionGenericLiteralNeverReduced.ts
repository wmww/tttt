// @strict: true
// An intersection with a generic instantiation of a type literal whose literal property conflicts with another
// member's is never (tsc's getReducedType looks at every member's properties): `Group<F> & Base` with Group's
// `type: "group"` against Base's `type?: never`. A type predicate's true branch intersects the declared union with
// the candidate's members (getNarrowedType's last resort), and tttt kept the Group members (youtube-enhancer)
type Keys = "a" | "b";
type Base<F extends Keys> = { id: F; type?: never; cb?: (x: F) => void };
type Group<F extends Keys> = { type: "group"; children: SNode<F>[] };
type Divider = { type: "divider" };
type TextN = { type: "text" };
type Setting<F extends Keys> =
  | (Base<F> & { component: "c1" }) | (Base<F> & { component: "c2" }) | (Base<F> & { component: "c3" })
  | (Base<F> & { component: "c4" }) | (Base<F> & { component: "c5" }) | (Base<F> & { component: "c6" })
  | (Base<F> & { component: "c7" }) | (Base<F> & { component: "c8" }) | (Base<F> & { component: "c9", options?: F[] });
type SNode<F extends Keys> = Divider | Group<F> | Setting<F> | TextN;
declare function isSetting<F extends Keys>(n: unknown): n is Setting<F>;
function render<F extends Keys>(node: SNode<F>) {
  if (isSetting(node)) {
    if (node.component === "c9") {
      const t: { a: 1 } = node;
    }
  }
}
type G2<F> = { type: "group"; children: F[] };
type H2<F> = { type: "other"; f: F };
function f<F>(a: G2<F> & { id: string; type?: never }, b: G2<F> & H2<F>, c: G2<F> & { type: "group" }) {
  const z: [1, 1, 1] = [a, b, c];
}
