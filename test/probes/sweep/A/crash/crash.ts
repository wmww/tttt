type Ctor<P> = new (props: P) => { props: P };
type Props<T> = T extends Ctor<infer P> ? P : {};
type Defaultize<P, D> = { [K in Exclude<keyof P, keyof D>]: P[K] };
type Managed<C, P> = C extends { defaultProps: infer D } ? Defaultize<P, D> : P;
interface Config<P> { get?: (props: P) => void }

declare function create<C extends Ctor<Props<C> & { relay: number }>>(c: C, config: Config<Managed<C, Props<C>>>): void;

declare class Feed { constructor(props: { relay: number; user: string }); props: { relay: number; user: string } }
create(Feed, { get(props) {} });
