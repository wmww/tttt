// @noTypesAndSymbols: true
// @filename: tsconfig.json
{"compilerOptions": {"strict": true, "noEmit": true, "target": "es2022", "module": "esnext", "moduleResolution": "bundler", "types": [], "moduleDetection": "force"}, "files": ["around_v1.ts", "around_v10.ts", "around_v2.ts", "around_v3.ts", "around_v4.ts", "around_v5.ts", "around_v6.ts", "around_v7.ts", "around_v8.ts", "around_v9.ts", "issue_index.nodirective.ts", "issue_index.ts", "part_enh_not_alias.ts", "part_enh_other_name.ts", "part_enh_plain_constraint.ts", "part_flow_enh_first.ts", "part_flow_enh_twice.ts", "part_flow_hoc_twice.ts", "part_flow_two_params.ts", "part_hoc_not_generic.ts", "part_hoc_other_name.ts", "part_hoc_unconstrained.ts", "part_no_M.ts", "part_no_N.ts", "part_original.ts", "part_return_C.ts", "part_return_getprops.ts", "part_self_constraint_only.ts", "part_simple_getprops.ts"]}
// @filename: around_v1.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh); export const p: never = R;
export const sentinel: number = "";
// @filename: around_v10.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
declare function two<B, C>(f: () => B, g: (b: B) => C): C; export const p: never = two(() => hoc, enh);
export const sentinel: number = "";
// @filename: around_v2.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
declare function one<B, C>(g: (b: B) => C): [B, C]; export const p: never = one(enh);
export const sentinel: number = "";
// @filename: around_v3.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
declare function ctx(g: (b: Comp<{}>) => unknown): void; ctx(enh);
export const sentinel: number = "";
// @filename: around_v4.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
declare function ctx2<B>(b: B, g: (b: B) => unknown): B; export const p: never = ctx2(hoc, enh);
export const sentinel: number = "";
// @filename: around_v5.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const p: never = enh(null! as Comp<{}>);
export const sentinel: number = "";
// @filename: around_v6.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const p: never = enh(hoc);
export const sentinel: number = "";
// @filename: around_v7.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
declare const x: Comp<GetProps<Comp<{}>>>; export const p: never = x;
export const sentinel: number = "";
// @filename: around_v8.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
function inner<C extends Comp<GetProps<C>>>(c: C) { const p: never = c; const q: never = null! as GetProps<C>; }
export const sentinel: number = "";
// @filename: around_v9.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
declare function flow1<A, B>(f: (a: A) => B): (a: A) => B; export const p: never = flow1(enh);
export const sentinel: number = "";
// @filename: issue_index.nodirective.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;

declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;

export const R = flow(hoc, enh);
// @filename: issue_index.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;

declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;

// @ts-expect-error
export const R = flow(hoc, enh);
// @filename: part_enh_not_alias.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & {}>>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_enh_other_name.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <E extends Comp<GetProps<E>>>(c: E) => Comp<M<GetProps<E> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_enh_plain_constraint.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<any>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_flow_enh_first.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(enh, hoc);
export const sentinel: number = '';
// @filename: part_flow_enh_twice.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(enh, enh);
export const sentinel: number = '';
// @filename: part_flow_hoc_twice.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, hoc);
export const sentinel: number = '';
// @filename: part_flow_two_params.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B>(f: (a: A) => B, g: (b: B) => unknown): A;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_hoc_not_generic.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: (c: Comp<{}>) => Comp<{}>;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_hoc_other_name.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <D extends Comp<any>>(c: D) => D;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_hoc_unconstrained.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_no_M.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<GetProps<C> & N>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_no_N.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C>>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_original.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_return_C.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => C;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_return_getprops.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? (C extends new () => 0 ? P : never) : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => GetProps<C>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_self_constraint_only.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? P : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => C;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
// @filename: part_simple_getprops.ts
type Comp<P> = (p: P) => void;
type M<T> = { [k in keyof T]: T };
type GetProps<C> = C extends Comp<infer P> ? P : never;
type Enh<I, N> = <C extends Comp<GetProps<C>>>(c: C) => Comp<M<GetProps<C> & N>>;
declare function flow<A, B, C>(f: (a: A) => B, g: (b: B) => C): (a: A) => C;
declare const hoc: <C extends Comp<any>>(c: C) => C;
declare const enh: Enh<{}, {}>;
export const R = flow(hoc, enh);
export const sentinel: number = '';
