// @strict: true
type Mutable<T extends object> = { -readonly [K in keyof T]: T[K] };
interface TextRange { pos: number; end: number }
interface Node extends TextRange { readonly kind: number; readonly parent: Node }
declare function setTextRange(r: TextRange, o: TextRange): void;
export function update<T extends Node>(updated: Mutable<T>, original: T) {
    setTextRange(updated, original);
    const r: TextRange = updated;
}
export function g<T extends Node>(updated: Mutable<T>) {
    const p = updated.pos;
    updated.pos = 1;
    return p;
}
