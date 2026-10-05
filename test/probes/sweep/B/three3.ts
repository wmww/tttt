declare class NodeClass {
    onUpdate(callback: (this: this) => unknown): this;
}
interface RefI extends NodeClass { property: string; }
declare const A: { new(): RefI & { tag: unknown } };
class DA extends A { r: number; }
declare const B: { new(): RefI & NodeClass };
class DB extends B { r: number; }
interface Other { onUpdate(callback: (this: this) => unknown): this; }
declare const C: { new(): NodeClass & Other };
class DC extends C { r: number; }
