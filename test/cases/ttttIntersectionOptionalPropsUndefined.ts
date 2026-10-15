// @strict: true
// The property of an intersection whose parts all have it optional intersects their types with undefined (tsc's
// getTypeOfSymbol of each part): a declared `v?: PV<O>` and a mapped `Pick`'s (the indexed access, `PV<O> | undefined`)
// are one type, which doesn't distribute (react-select's useStateManager(useAsync(props)) inferred Option candidates
// `Option & MultiValue<Option>` from the distributed one)
type PV<O> = readonly O[] | O | null;
interface PB<O> { value: PV<O>; other: string }
function f<O>(x: Partial<PB<O>> & Pick<Partial<PB<O>>, "value">, y: { value?: PV<O> } & Pick<{ value?: PV<O> }, "value">) {
    const n: number = x.value;
    const m: number = y.value;
}
declare function infer<O>(p: Partial<PB<O>> & { extra?: 1 }): O;
function g<O>(x: Partial<PB<O>> & Pick<Partial<PB<O>>, "value">) {
    const o: number = infer(x);
}
