// @strict: true
type ZodTrait = { _zod: { def: any; [k: string]: any } };
interface Ctor<T extends ZodTrait, D = T["_zod"]["def"]> {
  new (def: D): T;
  init(inst: T, def: D): asserts inst is T;
}
type ProtoOf<T> = {
  [K in keyof T]?: (T[K] extends (...args: infer A) => infer R ? (...args: A) => R : T[K]) | undefined;
} & ThisType<T>;
declare function ctor<T extends ZodTrait, D = T["_zod"]["def"]>(name: string, init: (inst: T, def: D) => void, proto?: ProtoOf<T>): Ctor<T, D>;
interface Inst { _zod: { def: { k: 1 } }; check(x: number): this; properties(s: string): Inst }
const I: Ctor<Inst> = ctor("I", (inst, def) => {}, {
  properties(s: string) { return this.check(1) as any; },
});
