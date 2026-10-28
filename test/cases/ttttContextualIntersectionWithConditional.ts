// origin: npm vvedantb/eva @ fd387003e apps/web (tsconfig.json), src/lib/components/ChangelogDialog.tsx:37 (convex/react useMutation(...).withOptimisticUpdate)
// flip: yes
// A contextual intersection of a type parameter and a conditional type: the conditional's apparent union (no call
// signatures in common) contributes none, the type parameter's constraint gives the signature (tsc's per-member
// getSignaturesOfType of an intersection)
// @strict: true
interface Store { get(n: string): number }
type OptimisticUpdate<Args> = (localStore: Store, args: Args) => void;
interface M<A> {
  withOptimisticUpdate<T extends OptimisticUpdate<A>>(u: T & (ReturnType<T> extends Promise<any> ? "must be sync" : {})): M<A>;
}
declare const m: M<{ id: string }>;
m.withOptimisticUpdate((localStore) => { localStore.get("x"); });
m.withOptimisticUpdate((localStore, args) => { localStore.get(args.id); });
