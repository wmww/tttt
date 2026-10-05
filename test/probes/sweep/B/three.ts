declare class NodeClass {
    onUpdate(callback: (this: this) => unknown): this;
}
interface RefI extends NodeClass { property: string; }
type Ref = RefI & NodeClass & { tag: unknown };
declare const Ref: { new(): Ref };
class Derived extends Ref {
    renderer: number;
}
