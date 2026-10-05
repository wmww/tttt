type RemoveIndex<T> = { [P in keyof T as P]: T[P] };
type Plain<T> = { [P in keyof T]: T[P] };
type CP<T> = { [K in keyof T as K]: T[K] };
declare function f1<T, M extends RemoveIndex<keyof CP<T>>>(m: M): CP<T>[M];
declare function f2<T, M extends Plain<keyof CP<T>>>(m: M): CP<T>[M];
declare function f3<T, M extends RemoveIndex<keyof T>>(m: M): T[M];
