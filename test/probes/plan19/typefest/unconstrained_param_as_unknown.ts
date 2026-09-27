// @strict: true
type JV = string | number | { [k: string]: JV } | JV[];
class Inner { m = new Map<string, number>(); }
type Y<J> = (() => J) extends () => JV ? 1 : 2;
const y: Y<{ inner: Inner }> = 2;
type Y2<J> = (() => J) extends () => string ? 1 : 2;
const y2: Y2<number> = 2;
type Y3<J> = [J] extends [JV] ? 1 : 2;
const y3: Y3<{ inner: Inner }> = 2;
