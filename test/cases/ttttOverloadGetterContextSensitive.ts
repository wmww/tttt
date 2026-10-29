// vue-query's mutationOptions getter overload (sourcesweep): tsgo gives the getter overload's arrow anyFunctionType in
// the first pass, as its contextual return type WithRequired<MO<number>, 'mutationKey'> can't contain type variables
// (an instantiation's flag comes from its type arguments), so the arrow is checked fully under that overload first
// @strict: true
interface MO<TData = unknown> {
    mutationFn?: () => Promise<TData>;
    mutationKey?: readonly unknown[];
    onSuccess?: (data: TData) => unknown;
    extra?: number;
}
type WithRequired<T, K extends keyof T> = T & { [_ in K]: {} };
declare function mo<TData = unknown>(o: WithRequired<MO<TData>, 'mutationKey'>): void;
declare function mo<TData = unknown>(o: () => WithRequired<MO<TData>, 'mutationKey'>): void;
declare function mo<TData = unknown>(o: Omit<MO<TData>, 'mutationKey'>): void;
declare function mo<TData = unknown>(o: () => Omit<MO<TData>, 'mutationKey'>): void;
mo(() => ({
    mutationFn: () => Promise.resolve(5),
    onSuccess: (data) => { const n: number = data; },
}));
