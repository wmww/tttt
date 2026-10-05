declare class NodeClass {
    onUpdate(callback: (this: this) => unknown): this;
}
interface RefI extends NodeClass { property: string; }
declare const Ref: { new(): RefI };
class Derived extends Ref {
    renderer: number;
}
declare const Ref2: { new(): NodeClass & { tag: unknown } };
class Derived2 extends Ref2 {
    renderer: number;
}
