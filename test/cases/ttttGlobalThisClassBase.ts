// A class (block-scoped) is no property of `globalThis`: `extends globalThis.C` is TS2339 and the class has no base, so
// nothing is checked against C's members (tinybench's `extends globalThis.Event` where Event is a class)
declare class Ev { get type(): string }
declare class B extends globalThis.Ev {
    type: string;
}
declare var V: typeof Ev;
declare class D extends globalThis.V {
    type: string;
}
