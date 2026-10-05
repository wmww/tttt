declare function bind<AX extends readonly unknown[], A extends readonly unknown[], R>(
    f: (...args: readonly [...bound: AX, ...args: A]) => R,
    ...bound: AX
): (...args: A) => R;
const r = bind((a: string, b: number) => {}, "foo");
