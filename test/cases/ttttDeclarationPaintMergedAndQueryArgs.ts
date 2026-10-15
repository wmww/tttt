// @declaration: true
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
