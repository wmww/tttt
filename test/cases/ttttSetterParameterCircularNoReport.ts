// @checkJs: true
// @strict: true
// @noEmit: true

// An unannotated setter parameter typed by its getter has the accessor's type: a circularity through the pair is the
// getter's TS7023 (and the property's TS7022), not a TS7022 for the parameter
// @Filename: thisPropertyAssignmentInherited.js
export class Element {
  /**
   * @returns {String}
   */
  get textContent() {
    return  ''
  }
  set textContent(x) {}
  cloneNode() { return this}
}
export class HTMLElement extends Element {}
export class TextElement extends Object {
  get innerHTML() { return this.textContent; }
  set innerHTML(html) { this.textContent = html; }
  toString() {
  }
}

