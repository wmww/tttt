// A field with an initializer reports its widening (TS7008) even when the constructor assigns it too (vscode
// outlineModel.ts 265 `readonly parent = undefined` under flagflip's nosnc)
// @strictNullChecks: false
// @noImplicitAny: true
export class C {
  readonly parent = undefined;
}
export class D {
  readonly parent = undefined;
  constructor() { this.parent = undefined; }
}
