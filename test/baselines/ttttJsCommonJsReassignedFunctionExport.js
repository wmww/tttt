//// [test/cases/ttttJsCommonJsReassignedFunctionExport.ts] ////

//// [a.js]
if (Math.random()) {
    exports.f = function (msg) { console.log(msg) }
} else {
    exports.f = function (msg) { }
}
exports.h = function () { };
exports.h.x = 1;
exports.h = function (x) { };
class C {}
C.k = function () {};
C.k = function (y) {};
module.exports.C = C;




//// [a.d.ts]
export declare var f: ((msg: any) => void) | ((msg: any) => void);
export declare var h: (() => void) | ((x: any) => void);
export { C };
declare class C {
}
declare namespace C {
    var k: (() => void) | ((y: any) => void);
    var k: (() => void) | ((y: any) => void);
}
