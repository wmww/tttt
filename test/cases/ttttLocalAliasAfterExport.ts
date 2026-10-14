// A local type alias after an exported one of its name merges into the export's local symbol: its type is the export's
// @module: commonjs
// @target: es2015
export type A = {}
type A = {}
let x: A;
export interface I {}
interface I { a: 1 }
let y: I;
