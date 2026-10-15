// @declaration: true
// @strict: true
// @target: es2022
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
