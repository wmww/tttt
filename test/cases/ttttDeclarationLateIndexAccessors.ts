// @declaration: true
// @strict: true
// Accessors whose computed names are of type symbol or string (not unique, not literal) are components of the index
// signature such names make, written as properties like fields and methods (tsc's createLateBoundIndexSignatures); a
// getter and a setter of one name are two symbols, two members. The import of the name is kept

// @filename: backref.ts
export declare const BACK_REF: symbol;

// @filename: a.ts
import { BACK_REF } from './backref';
declare function getProp(): any;
declare const k2: symbol;
declare const k3: string;
export class SsrNode {
  parentComponent: number = 1;
  get [BACK_REF]() { return getProp(); }
  get [k2]() { return 1; }
  set [k2](v: number) {}
  set [k3](v: string) {}
  static get [k3]() { return "a" as const; }
}
