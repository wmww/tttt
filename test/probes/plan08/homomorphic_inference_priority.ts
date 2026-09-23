// @strict: true
type Mutable<T extends object> = { -readonly [K in keyof T]: T[K] };
interface Node { readonly kind: number; readonly pos: number }
interface TypeNode extends Node { _t: any }
interface JSDocType extends TypeNode { _j: any }
declare function update<T extends Node>(updated: Mutable<T>, original: T): T;
declare function worker<T extends JSDocType & { readonly type: TypeNode | undefined }>(kind: T["kind"], type: T["type"]): T;
export function upd<T extends JSDocType & { readonly type: TypeNode | undefined }>(kind: T["kind"], node: T, type: T["type"]): T {
    return node.type !== type
        ? update(worker(kind, type), node)
        : node;
}
