// @strict: true
type JV = string | number | { [k: string]: JV } | JV[];
class Inner { m = new Map<string, number>(); }
class W {
  inner: Inner = new Inner();
  toJSON() { return { inner: this.inner }; }
}
type X<T> = T extends { toJSON(): infer J } ? ((() => J) extends () => JV ? 1 : 2) : 3;
const x1: X<W> = 2;
type X2<T> = T extends { toJSON(): infer J } ? (J extends JV ? 1 : 2) : 3;
const x2: X2<W> = 2;
type X3<T> = T extends { a: infer J } ? ((() => J) extends () => JV ? 1 : 2) : 3;
const x3: X3<{ a: { inner: Inner } }> = 2;
