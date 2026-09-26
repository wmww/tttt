// tsc: a1 a2 a4 a5 are T (g1/g2 generic: skipped in the first pass, then instantiated in the context of Resolver<V, C>
// with the fixing mapper, which fixes V = T from the spread), a3 is FV (non-generic: its FV candidates count)
type FV = Record<string, any>;
type Resolver<V extends FV, C> = (values: V, context: C | undefined) => V;
type Props<V extends FV, C = any> = Partial<{ values: V; resolver: Resolver<V, C> }>;
declare function useForm<V extends FV = FV, C = any>(props?: Props<V, C>): V;
declare const g1: <V extends FV>(values: V, context: unknown) => V;
declare const g2: <V extends FV, C>(values: FV, context: C | undefined) => FV;
declare const g3: (values: FV, context: unknown) => FV;
export function f2<T extends FV>(options: Props<T> | undefined, o2: Props<T>) {
  const a1 = useForm({ ...options, resolver: g1 });
  const a2 = useForm({ ...options, resolver: g2 });
  const a3 = useForm({ ...options, resolver: g3 });
  const a4 = useForm({ ...o2, resolver: g1 });
  const a5 = useForm({ values: o2.values!, resolver: g1 });
}
