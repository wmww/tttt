// @declaration: true
// @strict: true
// @target: es2022
// An inferred type reusing an annotation whose `typeof f<T>` can't be reused where it is printed: the failing part's
// cyclic structure placeholder fails the node around it (tsgo's recovery scope), which prints its type instead with
// no TS5088 (openclaw's `ReturnType<typeof createDeferred<unknown>>`), unless that type prints the part again
// (`Array<typeof f<T>>`) or the part is the whole annotation
// @filename: a.ts
function createDeferred<T>() {
    let resolve!: (v: T) => void;
    const promise = new Promise<T>(r => { resolve = r; });
    return { promise, resolve };
}
export function harness() {
    const ops: Array<{ result: ReturnType<typeof createDeferred<unknown>> }> = [];
    return { ops };
}
export function harnessArray() {
    const ops: { items: Array<typeof createDeferred<number>> }[] = [];
    return { ops };
}
export function harnessUnion() {
    const op: { result: ReturnType<typeof createDeferred<string>> | undefined } = { result: undefined };
    return op;
}
export function harnessDirect() {
    const op: { make: typeof createDeferred<boolean> } = { make: createDeferred };
    return op;
}
// @filename: b.ts
import { harness, harnessArray, harnessUnion, harnessDirect } from "./a";
export async function host() { return harness(); }
export function hostArray() { return harnessArray(); }
export function hostUnion() { return harnessUnion(); }
export function hostDirect() { return harnessDirect(); }
