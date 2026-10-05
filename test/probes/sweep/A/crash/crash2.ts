type Ctor<P> = (props: P) => void;
type Props<T> = T extends Ctor<infer P> ? P : {};
declare function create<C extends Ctor<Props<C> & { relay: number }>>(
    c: C, get: (props: { [K in Exclude<keyof Props<C>, "x">]: 0 }) => void): void;
declare function feed(props: { relay: number; user: string }): void;
create(feed, props => {});
