// @strict: true
// TS2774 (a tested function the body never calls) searches the `if` body / the ternary's true branch for uses of the
// tested function, reading the types of property accesses on the way. Done before the body was checked, that typed the
// parameters of function expressions in it without their contextual signature: spurious TS7006 (react-hook-form's
// validateField.ts, trpc/packages/openapi, dokploy; fixed in e332b875)
declare function k(cb: (name: string) => void): void;
declare const res: { appendHeader: (name: string, value: number) => void; m(): boolean };

function inIf(m: () => boolean) {
    if (m) {
        k((name) => { name.toLowerCase(); });
    }
}

function inTernary(m: () => boolean) {
    return m ? k((name) => { name.toLowerCase(); }) : undefined;
}

function assigned() {
    if (res.m) {
        res.appendHeader = (name, value) => { name.toLowerCase(); value.toFixed(); };
    }
}
