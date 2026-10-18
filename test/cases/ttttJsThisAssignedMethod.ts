// `this.f = this.f.bind(this)` in a JavaScript class declares nothing new: the method is the member (tsgo merges the
// assignment into the method's symbol, typed by the method), no TS7008
// @allowJs: true
// @checkJs: true
// @noEmit: true
// @strict: true
// @filename: a.js
export class C {
  m() {
    this.f = this.f.bind(this);
  }
  f() {}
}
export class D {
  m() {
    this.f = this.f.bind(this);
  }
  /** @param {number} ev */
  f(ev) {}
}
export class E {
  constructor() { this.open = false; }
  m() {
    this.f = this.f.bind(this);
  }
  f() { this.open = true; }
}
