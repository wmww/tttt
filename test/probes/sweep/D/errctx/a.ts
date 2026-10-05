import * as Popper from "missing-popper";
declare function g(cb: any): void;
g(x => x);
interface Opts { popperConfig: Partial<Popper.Options> | ((d: Partial<Popper.Options>) => Partial<Popper.Options>) | null }
declare function h(o: Opts): void;
h({ popperConfig: d => d });
declare function k(cb: Popper.Options): void;
k(y => y);
declare function m(cb: Partial<Popper.Options>): void;
m(z => z);
