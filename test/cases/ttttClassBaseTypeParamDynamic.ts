// @strict: true
// A class whose base is a type parameter's instance (`class C<t> extends (class {} as new <t>() => t)<t>`): its members
// depend on the type arguments (arktype's CastableBase), so a class extending an instantiation of a class extending it
// has the instantiated members (NodeCompiler's `data` and `ctx`)
const NoopBase = class {} as new <t extends object>() => t;
// @ts-ignore (as arktype: tsgo says TS2415, which tttt misses, see triage.md)
class CastableBase<t extends object> extends NoopBase<t> {
    declare private " covariant"?: t
}
class CompiledFunction<args extends readonly string[]> extends CastableBase<{ [k in args[number]]: k }> {}
export class NodeCompiler extends CompiledFunction<["data", "ctx"]> {
    f(): string { return this.data + this.ctx; }
}
