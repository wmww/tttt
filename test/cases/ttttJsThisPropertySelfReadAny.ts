// @strict: true
// @checkJs: true
// @noEmit: true

// A JS this-property every assignment of which reads the property itself has nothing to be typed by (tsgo's
// containsSameNamedThisProperty): any, and no implicit any reported (getWidenedTypeForAssignmentDeclaration's anyType)
// @filename: index.js
export class P {
  m() {
    this.Price = Number(this.Price);
  }
}
export class Q {
  m() {
    this.n = this.n + 1;
  }
}
export class R {
  m() {
    this.s = String(this.s);
    this.s.length;
  }
}
