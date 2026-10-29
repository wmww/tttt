// @strict: true
// The first pass's applicability check relates a function that is context sensitive only through what it returns as
// its return-only type (tsc's contextFreeTypes), not anyFunctionType: `undefined | anyFunctionType` doesn't fit, so the
// candidate fails before the second pass and the whole argument is reported (TS2345), T fixed by the return type
declare function useMemo<T>(func: () => T): T;
declare const c: boolean;
const p: (input: string) => boolean = useMemo(() => { if (c) return; return x => x.length > 0; });
const q: (input: string) => boolean = useMemo(() => { if (c) return 1; return x => x.length > 0; });
// (All returns functions: the first pass infers, and the declaration is what fails, in both)
const r: (input: string) => boolean = useMemo(() => { if (c) return (y: string) => 1; return x => x.length > 0; });
