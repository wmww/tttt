type Eq<L, R> = (<T>() => T extends (L & T) | T ? true : false) extends <T>() => T extends (R & T) | T ? true : false ? true : false;
type Prettify<T> = { [K in keyof T]: T[K] } & {};
type M<T> = { [K in keyof T]: T[K] };

type E1 = Eq<Prettify<{ key: number }>, { key: number }>;
type E2 = Eq<M<{ key: number }>, { key: number }>;
type E3 = Eq<M<Readonly<{ key: number }> & Pick<{ key: number }, 'key'>>, { key: number }>;
type E4 = Eq<M<Readonly<{ key: number }>>, { readonly key: number }>;
type E5 = Eq<M<{ key: number } & { key: number }>, { key: number }>;
type E6 = Eq<M<Pick<{ key: number }, 'key'>>, { key: number }>;
type E7 = Eq<M<{ readonly key: number } & { key: number }>, { key: number }>;
declare let x: {readonly key:number}&{key:number};
x.key = 1;

declare let y: M<{readonly key:number}&{key:number}>;
y.key = 1;
