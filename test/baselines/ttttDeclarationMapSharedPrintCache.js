//// [test/cases/ttttDeclarationMapSharedPrintCache.ts] ////

//// [ttttDeclarationMapSharedPrintCache.ts]
// tsgo's serializedTypes cache is per enclosing declaration: a class's property declarations (parameter properties
// included) share the class's, a function's parameters share the function's, while a method, an accessor and a return
// type have their own. A type printed again from the same cache is an unmapped clone (xterm's CoreTerminal
// `onRender` after `_onRender`).
declare function mk<T>(): T;
const o = mk<{ s: number, e: number }>();
export function f(a = o, b = o) { return o; }
export class D {
  m(a = o) { return o; }
  constructor(public p = o, q = o) {}
  x = o;
  get g() { return o; }
  y = o;
  [k: string]: any;
}
export class E {
    a = mk<{ s: number, e: number }>();
    b = this.a;
}


//// [ttttDeclarationMapSharedPrintCache.js]
const o = mk();
export function f(a = o, b = o) { return o; }
export class D {
    p;
    m(a = o) { return o; }
    constructor(p = o, q = o) {
        this.p = p;
    }
    x = o;
    get g() { return o; }
    y = o;
}
export class E {
    a = mk();
    b = this.a;
}


//// [ttttDeclarationMapSharedPrintCache.d.ts]
export declare function f(a?: {
    s: number;
    e: number;
}, b?: {
    s: number;
    e: number;
}): {
    s: number;
    e: number;
};
export declare class D {
    p: {
        s: number;
        e: number;
    };
    m(a?: {
        s: number;
        e: number;
    }): {
        s: number;
        e: number;
    };
    constructor(p?: {
        s: number;
        e: number;
    }, q?: {
        s: number;
        e: number;
    });
    x: {
        s: number;
        e: number;
    };
    get g(): {
        s: number;
        e: number;
    };
    y: {
        s: number;
        e: number;
    };
    [k: string]: any;
}
export declare class E {
    a: {
        s: number;
        e: number;
    };
    b: {
        s: number;
        e: number;
    };
}
//# sourceMappingURL=ttttDeclarationMapSharedPrintCache.d.ts.map