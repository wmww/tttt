function g<T extends any[]>(x: Partial<T>, y: Readonly<T>) {
    const a: readonly any[] = x;
    const b: readonly any[] = y;
    x.length;
}
type M<TArgs extends any[]> = [void] extends TArgs ? Partial<TArgs> : TArgs;
declare function f<TArgs extends any[]>(cb: (...args: TArgs) => void, ...args: M<TArgs>): void;
declare function h<TArgs extends any[]>(...args: Partial<TArgs>): void;
f((a: number) => {}, 1);
