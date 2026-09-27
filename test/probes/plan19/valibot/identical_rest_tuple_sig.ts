type SE<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type A = SE<(...args: [a: number]) => string, (a: number) => string>;
type B = SE<(...args: [a: number, ...rest: boolean[]]) => string, (a: number, ...rest: boolean[]) => string>;
type C = SE<(...args: [a: number, b?: string]) => string, (a: number, b?: string) => string>;
type D = SE<{ (a: number): string }, (a: number) => string>;
type E = SE<{ (a: number): string; k: 1 }, ((a: number) => string) & { k: 1 }>;
