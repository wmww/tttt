// @strict: true
// @target: es2015
export class Base<V> { private _m = new Map<string, V>(); get(k: string) { return this._m.get(k); } }
class Multi<V> extends Base<V[]> { add(k: string, v: V) { return v; } }
const m = new Multi<number>();
m.add("a", 1);
class Plain extends Base<string> {}
const p = new Plain();
