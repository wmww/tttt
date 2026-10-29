// @strict: true
// A quoted module without `declare` in a module (TS1035) binds as an augmentation but merges into nothing (bun case
// 01-C6b): tsc's collectModuleReferences lists a file's augmentations only when ambient, so X doesn't get `y`, and the
// body isn't checked as an augmentation's
// @filename: b.ts
export interface X { x: number }
// @filename: a.ts
import { X } from "./b";
module "./b" { export interface X { y: number } export default interface D {} }
declare const x: X;
x.y;
x.x;
module "./nowhere" { export interface Z {} }
