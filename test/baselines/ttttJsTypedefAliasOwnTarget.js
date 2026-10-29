//// [test/cases/ttttJsTypedefAliasOwnTarget.ts] ////

//// [ModuleGraphConnection.js]
/** @typedef {typeof T} T */
module.exports = class ModuleGraphConnection {};
module.exports.T = T;

//// [repro.js]
'use strict';
/** @typedef {import('./local-lib/ModuleGraphConnection')} ImportedType */
/** @type {ImportedType} */
module.exports = class Repro {};


//// [ModuleGraphConnection.js]
"use strict";
/** @typedef {typeof T} T */
module.exports = class ModuleGraphConnection {
};
module.exports.T = T;
//// [repro.js]
'use strict';
/** @typedef {import('./local-lib/ModuleGraphConnection')} ImportedType */
/** @type {ImportedType} */
module.exports = class Repro {
};


//// [ModuleGraphConnection.d.ts]
export = ModuleGraphConnection;
/** @typedef {typeof T} T */
declare class ModuleGraphConnection {
}
declare namespace ModuleGraphConnection {
    export var T: any;
}
export type T = typeof T;
//// [repro.d.ts]
export = Repro;
/** @typedef {import('./local-lib/ModuleGraphConnection')} ImportedType */
/** @type {ImportedType} */
declare class Repro {
}
export type ImportedType = import('./local-lib/ModuleGraphConnection');
