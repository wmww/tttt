//// [test/cases/ttttDeclarationPaintMergedAndQueryArgs.ts] ////

//// [ttttDeclarationPaintMergedAndQueryArgs.ts]
// Painting: a local type merged with an exported value of its name (tsc's binder merges them into one local
// symbol), and the type arguments of a type query (`typeof f<T>`)
export const Kind = { A: 'a', B: 'b' } as const;
type Kind = (typeof Kind)[keyof typeof Kind];
export interface M { [Kind.A]?: number; }
declare function Table<T>(x: T): T;
type User = { id: string };
type Story = Array<typeof Table<User>>;
export default {} as Story;
type One = ">" | "<";
export type Start<s extends string> = s extends `${infer c}${string}` ? c extends One ? [c & One] : never : never;
const enum TraceType { None, Creation }
export class Trace { constructor(public type: TraceType) {} static make() { return new Trace(TraceType.None); } }


//// [ttttDeclarationPaintMergedAndQueryArgs.js]
// Painting: a local type merged with an exported value of its name (tsc's binder merges them into one local
// symbol), and the type arguments of a type query (`typeof f<T>`)
export const Kind = { A: 'a', B: 'b' };
export default {};
export class Trace {
    type;
    constructor(type) {
        this.type = type;
    }
    static make() { return new Trace(0 /* TraceType.None */); }
}


//// [ttttDeclarationPaintMergedAndQueryArgs.d.ts]
export declare const Kind: {
    readonly A: 'a';
    readonly B: 'b';
};
type Kind = (typeof Kind)[keyof typeof Kind];
export interface M {
    [Kind.A]?: number;
}
declare function Table<T>(x: T): T;
type User = {
    id: string;
};
type Story = Array<typeof Table<User>>;
declare const _default: Story;
export default _default;
type One = ">" | "<";
export type Start<s extends string> = s extends `${infer c}${string}` ? c extends One ? [c & One] : never : never;
declare const enum TraceType {
    None = 0,
    Creation = 1
}
export declare class Trace {
    type: TraceType;
    constructor(type: TraceType);
    static make(): Trace;
}
