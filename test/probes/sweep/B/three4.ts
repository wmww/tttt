declare class NodeClass {
    onUpdate(callback: (this: this) => unknown): this;
}
interface Other { onUpdate(callback: (this: this) => unknown): this; }
declare const C: { new(): NodeClass & Other };
class DC extends C { r: number; }
declare const dc: DC;
const u = dc.onUpdate;
type T = (NodeClass & Other)["onUpdate"];
interface I extends NodeClass, Other { }
declare const i: I;
const v = i.onUpdate;
