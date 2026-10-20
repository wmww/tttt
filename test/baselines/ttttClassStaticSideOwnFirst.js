//// [test/cases/ttttClassStaticSideOwnFirst.ts] ////

//// [base.ts]
export declare class Base<P> { static contextType?: number; static propTypes?: any; p: P }
//// [a.ts]
import { Base } from "./base";
export const E = (() => class E extends Base<{ a: 1 }> {
  state = { error: false }
  static g = () => ({ error: true })
})()
export const F = class F extends Base<{ a: 1 }> {
  static g = () => ({ error: true })
}


//// [base.js]
export {};
//// [a.js]
import { Base } from "./base";
export const E = (() => class E extends Base {
    state = { error: false };
    static g = () => ({ error: true });
})();
export const F = class F extends Base {
    static g = () => ({ error: true });
};


//// [base.d.ts]
export declare class Base<P> {
    static contextType?: number;
    static propTypes?: any;
    p: P;
}
//// [a.d.ts]
export declare const E: {
    new (): {
        p: {
            a: 1;
        };
        state: {
            error: boolean;
        };
    };
    propTypes?: any;
    g: () => {
        error: boolean;
    };
    contextType?: number;
};
export declare const F: {
    new (): {
        p: {
            a: 1;
        };
    };
    g: () => {
        error: boolean;
    };
    contextType?: number;
    propTypes?: any;
};
