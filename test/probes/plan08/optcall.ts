// @strict: true
interface H { get?(): number; f?: () => string }
declare const h: H;
const a = h.get?.();
const b = h.f?.();
let cleanup: (() => void) | undefined;
cleanup?.();
declare function mk(): (() => void) | undefined;
const c = mk();
c?.();
