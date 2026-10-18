// @strict: true
// @noTypesAndSymbols: true
// @noEmit: true
// xterm's addons, qwik's SSRContainer (clidiff): a class or interface that fails its heritage clause names both types
// in the head as reportRelationError does, fully qualified when they would print the same
// @filename: b.ts
export declare class Addon { dispose(): void; activate(): void; }
export interface Shape { dispose(): void; }
export declare class Plain { dispose(): void; }

// @filename: a.ts
import { Addon as Base, Shape as ShapeBase, Plain as PlainBase } from './b';
export class Addon implements Base {
    activate(): void {}
}
export class Shape implements ShapeBase {
}
export class Plain extends PlainBase {
    dispose = 1;
}
export interface Mixed extends ShapeBase { dispose: number }

// @filename: c.ts
import type { Shape as S } from './b';
export interface Shape extends S { dispose: string }
