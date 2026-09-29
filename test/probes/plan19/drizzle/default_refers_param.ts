type Ex<T> = { ex: T };
class Base<TKind extends string, TFull extends object = {}, TSchema = Ex<TFull>> {
    s!: TSchema;
}
class D<TSchema extends object> extends Base<"a", TSchema> {
    m() { const x: number = this.s; }
}
class D2<TFull extends object> extends Base<"a", TFull> {
    m() { const x: number = this.s; }
}
declare const b: Base<"a", { q: 1 }>;
const y: number = b.s;
