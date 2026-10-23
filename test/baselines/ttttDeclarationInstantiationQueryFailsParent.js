//// [test/cases/ttttDeclarationInstantiationQueryFailsParent.ts] ////

//// [a.ts]
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
//// [b.ts]
import { harness, harnessArray, harnessUnion, harnessDirect } from "./a";
export async function host() { return harness(); }
export function hostArray() { return harnessArray(); }
export function hostUnion() { return harnessUnion(); }
export function hostDirect() { return harnessDirect(); }


//// [a.js]
function createDeferred() {
    let resolve;
    const promise = new Promise(r => { resolve = r; });
    return { promise, resolve };
}
export function harness() {
    const ops = [];
    return { ops };
}
export function harnessArray() {
    const ops = [];
    return { ops };
}
export function harnessUnion() {
    const op = { result: undefined };
    return op;
}
export function harnessDirect() {
    const op = { make: createDeferred };
    return op;
}
//// [b.js]
import { harness, harnessArray, harnessUnion, harnessDirect } from "./a";
export async function host() { return harness(); }
export function hostArray() { return harnessArray(); }
export function hostUnion() { return harnessUnion(); }
export function hostDirect() { return harnessDirect(); }


//// [a.d.ts]
declare function createDeferred<T>(): {
    promise: Promise<T>;
    resolve: (v: T) => void;
};
export declare function harness(): {
    ops: {
        result: ReturnType<typeof createDeferred<unknown>>;
    }[];
};
export declare function harnessArray(): {
    ops: {
        items: Array<typeof createDeferred<number>>;
    }[];
};
export declare function harnessUnion(): {
    result: ReturnType<typeof createDeferred<string>> | undefined;
};
export declare function harnessDirect(): {
    make: typeof createDeferred<boolean>;
};
export {};
