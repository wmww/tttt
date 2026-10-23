// @strict: true
// KNOWN FAILING (bugs/sourcesweep_checker.md, opencode leftovers: starlight's schema.ts:119). tsgo reports TS5114
// at the alias: its constraint check of the type argument resolves B's members, whose base's argument B["_input"]
// resolves them again from the start until the instantiation stack is 100 deep. tttt reports at the use: TS5114
// from members_reentered_unguarded()'s synthesized stack, then a real TS2589 from the instantiated property's
// re-entry, two hits where tsgo has one
declare abstract class ZodType<Input = any> {
    readonly _input: Input;
}
declare class ZodUnion<T extends readonly ZodType[]> extends ZodType<T[number]["_input"]> {}
type B = ZodUnion<[B, ...B[]]>;
declare const b: B;
const i = b._input;
