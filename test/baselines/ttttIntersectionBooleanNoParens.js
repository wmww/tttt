//// [test/cases/ttttIntersectionBooleanNoParens.ts] ////

//// [ttttIntersectionBooleanNoParens.ts]
// `boolean` and a whole enum in an intersection print as names: no parentheses
enum E { A, B }
declare function f<T>(x: T): T & { tag: 1 };
export const a = f(Math.random() > 0.5);
export const b = f(E.A as E);


//// [ttttIntersectionBooleanNoParens.js]
// `boolean` and a whole enum in an intersection print as names: no parentheses
var E;
(function (E) {
    E[E["A"] = 0] = "A";
    E[E["B"] = 1] = "B";
})(E || (E = {}));
export const a = f(Math.random() > 0.5);
export const b = f(E.A);


//// [ttttIntersectionBooleanNoParens.d.ts]
declare enum E {
    A = 0,
    B = 1
}
export declare const a: boolean & {
    tag: 1;
};
export declare const b: E & {
    tag: 1;
};
export {};
