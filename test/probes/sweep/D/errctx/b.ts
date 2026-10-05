import * as Popper from "missing-popper";
type P = Partial<Popper.Options>;
declare const p: P;
type U = Partial<Popper.Options> | ((d: number) => void) | null;
declare const u: U;
declare const q: Partial<any>;
