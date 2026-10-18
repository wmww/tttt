//// [test/cases/ttttMergedOwnerPossiblyReferenced.ts] ////

//// [ttttMergedOwnerPossiblyReferenced.ts]
// tsc's isTypeParameterPossiblyReferenced: a type parameter (or `this`) of a class or interface declared more than once
// is possibly referenced by every node in it, so a method's parameter types are instantiated with the class even when
// they mention none of its type parameters: printed anew (`| undefined`), not reused (react-three-fiber's
// React.Component setState/forceUpdate). Whichever lowers the nodes first, the class's walk or its members
declare class Component<P, S> {
    constructor(props: P);
    setState(state: S | null, callback?: () => void): void;
    forceUpdate(callback?: () => void): void;
    props: P;
}
interface Component<P, S> { extra?(): void }
export const E = (() => class E extends Component<{ a: 1 }, { e: boolean }> {})();
export class F extends Component<{ a: 1 }, { e: boolean }> {}
export const f = new F({ a: 1 });
export const fu = f.forceUpdate;


//// [ttttMergedOwnerPossiblyReferenced.js]
export const E = (() => class E extends Component {
})();
export class F extends Component {
}
export const f = new F({ a: 1 });
export const fu = f.forceUpdate;


//// [ttttMergedOwnerPossiblyReferenced.d.ts]
declare class Component<P, S> {
    constructor(props: P);
    setState(state: S | null, callback?: () => void): void;
    forceUpdate(callback?: () => void): void;
    props: P;
}
interface Component<P, S> {
    extra?(): void;
}
export declare const E: {
    new (props: {
        a: 1;
    }): {
        setState(state: {
            e: boolean;
        } | null, callback?: (() => void) | undefined): void;
        forceUpdate(callback?: (() => void) | undefined): void;
        props: {
            a: 1;
        };
        extra?(): void;
    };
};
export declare class F extends Component<{
    a: 1;
}, {
    e: boolean;
}> {
}
export declare const f: F;
export declare const fu: (callback?: (() => void) | undefined) => void;
export {};
