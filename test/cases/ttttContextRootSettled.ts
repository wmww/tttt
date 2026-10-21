// A JavaScript this-property assigned from a callback's parameter, read in an earlier method, where the callback is
// nested in the statement (a chained call, an object literal, another callback), in a declaration's initializer, a
// return expression or a condition: its context root is checked first (tsc's getContextuallyTypedParameterType
// resolves the call on demand), not an implicit any
// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: /a.js
export class Build {
  read() {
    return [this.out, this.len, this.obj, this.nested, this.ret, this.init];
  }
  /** @param {string[]} xs  @param {{ on(h: { cb(s: string): void }): void }} em */
  setup(xs, em) {
    xs.map((s) => s).forEach((x) => { this.out = x; });
    em.on({ cb: (s) => { this.obj = s; } });
    xs.forEach((a) => xs.forEach((b) => { this.nested = b; }));
    const f = () => xs.forEach((r) => { this.ret = r; });
    const g = xs.map((i) => { this.init = i; return i; });
    if (xs.some(({ length }) => { this.len = length; return true; })) {}
  }
}
