// @allowJs: true
// @checkJs: true
// @strict: true
// @noEmit: true
// A `this.x` member declared only in methods is typed under its symbol's type resolution like any assignment
// declaration (tsgo's getWidenedTypeForAssignmentDeclaration): `{ filePath: relPath }` flow-narrows relPath across
// `this.fileCache?.filePath === relPath`, whose left operand reads fileCache again: TS7022 at the first assignment
// (knip's language server). Not a member declared in the constructor, typed by control flow there
// @filename: a.js
export class S {
  stop() {
    this.fileCache = undefined;
  }
  /** @param {string} relPath */
  get(relPath) {
    if (this.fileCache?.filePath === relPath) return 1;
    this.fileCache = { filePath: relPath };
  }
}
export class C {
  constructor() {
    this.count = 0;
    this.count = this.count + 1;
  }
}
