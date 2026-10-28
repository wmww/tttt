// @noUnusedLocals: true
// A private method destructured from `this` by an assignment directly in itself isn't a read of it (tsc's
// markPropertyAsReferenced: the nearest function-like ancestor is the method; o), but in an arrow function nested in it
// (p) it is, and a binding element always reads (m, n). Was by position: a function nested in m counted as m
class A {
    private m() {
        const { m } = this;
        return m;
    }
    private n() {
        const f = () => {
            const { n } = this;
            return n;
        };
        return f;
    }
    private o() {
        let o: unknown;
        ({ o } = this);
        return o;
    }
    private p() {
        const f = () => {
            let p: unknown;
            ({ p } = this);
            return p;
        };
        return f;
    }
}
export {};
