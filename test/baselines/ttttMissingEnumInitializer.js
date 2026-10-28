//// [test/cases/ttttMissingEnumInitializer.ts] ////

//// [ttttMissingEnumInitializer.ts]
abstract class A {
    abstract a = ;
    declare d = ;
    static p = ;
    q: string = ;
    constructor() { this.q; }
}
const enum CE { X = , Y }
declare enum DE { X = , Y }
let v = CE.X;
class B { r = ; m() { return this.r; } }


//// [ttttMissingEnumInitializer.js]
"use strict";
class A {
    static p = ;
    q = ;
    constructor() { this.q; }
}
let v = CE.X;
class B {
    r = ;
    m() { return this.r; }
}


//// [ttttMissingEnumInitializer.d.ts]
declare abstract class A {
    abstract a: any;
    d: any;
    static p: any;
    q: string;
    constructor();
}
declare const enum CE {
    X,
    Y
}
declare enum DE {
    X,
    Y
}
declare let v: CE;
declare class B {
    r: any;
    m(): any;
}
