//// [test/cases/ttttDeclarationLateIndexAccessors.ts] ////

//// [backref.ts]
export declare const BACK_REF: symbol;

//// [a.ts]
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


//// [backref.js]
export {};
//// [a.js]
import { BACK_REF } from './backref';
export class SsrNode {
    parentComponent = 1;
    get [BACK_REF]() { return getProp(); }
    get [k2]() { return 1; }
    set [k2](v) { }
    set [k3](v) { }
    static get [k3]() { return "a"; }
}


//// [backref.d.ts]
export declare const BACK_REF: symbol;
//// [a.d.ts]
import { BACK_REF } from './backref';
declare const k2: symbol;
declare const k3: string;
export declare class SsrNode {
    static [k3]: "a";
    [k3]: string;
    [BACK_REF]: any;
    [k2]: number;
    [k2]: number;
    parentComponent: number;
}
export {};
