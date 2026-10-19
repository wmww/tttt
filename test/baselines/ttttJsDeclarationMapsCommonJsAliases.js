//// [test/cases/ttttJsDeclarationMapsCommonJsAliases.ts] ////

//// [dep.js]
module.exports = { v: 1 };

//// [a.js]
const dep = require('./dep')
function loadConfig() { return dep.v }
function toposort() { return 1 }
module.exports.loadConfig = loadConfig
module.exports.array = toposort
module.exports.dep = dep

//// [c.js]
const q = 1
exports.o = { v: 1 }
module.exports.p = { w: q }




//// [dep.d.ts]
declare const _exports: {
    v: number;
};
export = _exports;
//# sourceMappingURL=dep.d.ts.map//// [a.d.ts]
export { loadConfig };
export { toposort as array };
export { dep };
import dep = require('./dep');
declare function loadConfig(): number;
declare function toposort(): number;
//# sourceMappingURL=a.d.ts.map//// [c.d.ts]
export declare var o: {
    v: number;
};
export declare var p: {
    w: number;
};
//# sourceMappingURL=c.d.ts.map