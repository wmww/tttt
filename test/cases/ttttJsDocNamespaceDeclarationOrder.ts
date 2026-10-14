// @allowJs: true
// The namespace a dotted `@typedef A.b` declares is bound in source order (tsgo reparses it into a ModuleDeclaration
// next to its host statement), not deferred like a top-level typedef: `export {A}` lists the typedefs before it first
// @checkJs: true
// @noEmit: true
// @filename: file.js
const myTypes = {};
/** @typedef {string} myTypes.typeA */
export {myTypes};
/** @typedef {number} myTypes.typeB */
const x = 1;
/** @typedef {number} myTypes.typeC */
// @filename: file2.js
import {myTypes} from './file.js';
