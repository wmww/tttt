// A JavaScript this-property assigned from a callback's parameter, read in an earlier method: the parameter's type is
// asked for before its call statement is checked, and takes its contextual type then (tsc's
// getContextuallyTypedParameterType resolves the call on demand), not an implicit any
// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// @filename: /a.js
export class Build {
  read() {
    return this.out;
  }
  /** @param {string[]} xs */
  setup(xs) {
    xs.forEach((x) => { this.out = x; });
    xs.forEach(({ length }) => { this.len = length; });
  }
  read2() {
    return this.len;
  }
}
