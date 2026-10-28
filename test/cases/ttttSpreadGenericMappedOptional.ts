// A generic mapped type is no empty object (tsc's isEmptyObjectType), so `{ ...x }` of `Omit<T["p"], "id"> | undefined`
// is tryMergeUnionOfObjectTypeAndEmptyObject's optional copy of its properties (none known), not the generic type: the
// literal isn't assignable to `T["p"]` (flagflip vscode/exactOptionalPropertyTypes, mcpServerRequestHandler.ts)
// @strict: true
// @exactOptionalPropertyTypes: true
interface P { _meta?: {}; cursor?: string }
interface PR { method: string; params?: P }
function g<T extends PR>(init?: Omit<T['params'], 'id'>) {
    const d: T['params'] = { ...init, cursor: undefined };
    const e: T['params'] = { ...init };
    const f = { ...init, cursor: undefined };
}
function h<T extends PR>(init: Omit<T['params'], 'id'> | undefined) {
    const d: T['params'] = { ...init, cursor: undefined };
}
interface P2 { _meta?: {}; cursor?: string }
interface R1 { method: "a"; params?: P2 }
interface R2 { method: "b"; params: { x: number } }
interface PR2 { method: string; params?: P2 }
function f2<T extends PR2 & (R1 | R2)>(init?: Omit<T['params'], 'id'>) {
    let next: string | undefined = undefined;
    const params: T['params'] = { ...init, cursor: next };
}
function g2<T extends PR2>(init?: Omit<T['params'], 'id'>) {
    let next: string | undefined = undefined;
    const params: T['params'] = { ...init, cursor: next };
}
function h2<T extends PR2>() {
    let next: string | undefined = undefined;
    const params: T['params'] = { cursor: next };
}
