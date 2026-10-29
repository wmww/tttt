// vue-query's mutationOptions getter overload (sourcesweep): the arrow's inner parameter is typed by the failing
// object overload first in tttt (data: any, TS7006), by the getter overload in tsgo (known failing, sourcesweep_checker.md)
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
