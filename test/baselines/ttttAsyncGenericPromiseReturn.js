//// [test/cases/ttttAsyncGenericPromiseReturn.ts] ////

//// [ttttAsyncGenericPromiseReturn.ts]
// An async function returning a generic that could be a promise keeps it: the return expression awaits to Awaited<T>,
// which the inferred Promise<...> unwraps (tsc's checkAwaitedType then unwrapAwaitedType), not to its constraint's
// promised type (any)
export async function f1<T extends () => Promise<any>>() { return null! as Promise<ReturnType<T>>; }
export async function f2<T extends () => Promise<number>>() { return null! as Promise<ReturnType<T>>; }
export async function f3<T extends Promise<any>>() { return null! as Promise<T>; }
export async function f4<T extends Record<string, () => Promise<any>>, K extends keyof T>() { return null! as Promise<ReturnType<T[K]>>; }
export async function f5<T extends () => Promise<any>>() { return null! as ReturnType<T>; }
export async function f6<T extends Record<string, () => Promise<any>>, K extends keyof T>() { return null! as ReturnType<T[K]>; }
type CustomEvents = Record<string, (documentName: string, payload: any) => Promise<any>>;
export class X<TCE extends CustomEvents> {
  constructor(private readonly ev: TCE) {}
  private async local<TName extends Extract<keyof TCE, string>>(n: TName) {
    const result = await this.ev[n]("", 1);
    return result as Promise<ReturnType<TCE[TName]>>;
  }
  async h1<TName extends Extract<keyof TCE, string>>(n: TName) {
    return this.local(n);
  }
  async h2<TName extends Extract<keyof TCE, string>>(n: TName, b: boolean) {
    if (b) return;
    return null! as Promise<ReturnType<TCE[TName]>>;
  }
  async h3<TName extends Extract<keyof TCE, string>>(n: TName, b: boolean) {
    if (b) return;
    return this.local(n);
  }
}


//// [ttttAsyncGenericPromiseReturn.js]
// An async function returning a generic that could be a promise keeps it: the return expression awaits to Awaited<T>,
// which the inferred Promise<...> unwraps (tsc's checkAwaitedType then unwrapAwaitedType), not to its constraint's
// promised type (any)
export async function f1() { return null; }
export async function f2() { return null; }
export async function f3() { return null; }
export async function f4() { return null; }
export async function f5() { return null; }
export async function f6() { return null; }
export class X {
    ev;
    constructor(ev) {
        this.ev = ev;
    }
    async local(n) {
        const result = await this.ev[n]("", 1);
        return result;
    }
    async h1(n) {
        return this.local(n);
    }
    async h2(n, b) {
        if (b)
            return;
        return null;
    }
    async h3(n, b) {
        if (b)
            return;
        return this.local(n);
    }
}


//// [ttttAsyncGenericPromiseReturn.d.ts]
export declare function f1<T extends () => Promise<any>>(): Promise<ReturnType<T>>;
export declare function f2<T extends () => Promise<number>>(): Promise<ReturnType<T>>;
export declare function f3<T extends Promise<any>>(): Promise<T>;
export declare function f4<T extends Record<string, () => Promise<any>>, K extends keyof T>(): Promise<ReturnType<T[K]>>;
export declare function f5<T extends () => Promise<any>>(): Promise<ReturnType<T>>;
export declare function f6<T extends Record<string, () => Promise<any>>, K extends keyof T>(): Promise<ReturnType<T[K]>>;
type CustomEvents = Record<string, (documentName: string, payload: any) => Promise<any>>;
export declare class X<TCE extends CustomEvents> {
    private readonly ev;
    constructor(ev: TCE);
    private local;
    h1<TName extends Extract<keyof TCE, string>>(n: TName): Promise<ReturnType<TCE[TName]>>;
    h2<TName extends Extract<keyof TCE, string>>(n: TName, b: boolean): Promise<ReturnType<TCE[TName]> | undefined>;
    h3<TName extends Extract<keyof TCE, string>>(n: TName, b: boolean): Promise<ReturnType<TCE[TName]> | undefined>;
}
export {};
