// @strict: true
interface TypeNode { t: number }
interface JSDocType { kind: number }
declare function paren(t: TypeNode): TypeNode;
declare function worker<T extends JSDocType & { readonly type: TypeNode | undefined }>(kind: T["kind"], type: T["type"]): T;
export function f<T extends JSDocType & { readonly type: TypeNode | undefined }>(kind: T["kind"], type: T["type"], postfix: boolean) {
    return worker(kind, postfix ? type && paren(type) : type);
}
