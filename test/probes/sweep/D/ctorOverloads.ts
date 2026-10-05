// @strict: true
interface Component<P = {}, S = {}, SS = any> {}
declare class Component<P, S> {
    constructor(props: P);
    constructor(props: P, context: any);
    readonly props: Readonly<P>;
    state: Readonly<S>;
}
type Ctor<P> = ((props: P) => string) | (new (props: P, context: any) => Component<any, any>);
interface Exact { value: "A" | "B" }
interface Narrower { value: "A" }
let W: Ctor<Exact>;
W = class N extends Component<Narrower> {};
