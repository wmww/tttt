//// [test/cases/ttttDeclarationCyclicMappedAlias.ts] ////

//// [fastdom.d.ts]
type Omit<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>>;

declare class Fastdom {
  clear<T extends () => void>(task: T): boolean;
  extend<T extends object>(props: T): Omit<this, keyof T & keyof this> & T;
  catch: null | ((e: unknown) => any);
}

declare const fastdom: Fastdom

export default fastdom;

//// [a.ts]
import fastdomModule from './fastdom';
export const fastdom = fastdomModule.extend({ raf(cb: () => void) {} });


//// [a.js]
import fastdomModule from './fastdom';
export const fastdom = fastdomModule.extend({ raf(cb) { } });


//// [a.d.ts]
export declare const fastdom: {
    clear: <T extends () => void>(task: T) => boolean;
    extend: <T extends object>(props: T) => (Exclude<"catch", keyof T & keyof {
        clear<T_2 extends () => void>(task: T_2): boolean;
        extend<T extends object>(props: T): Pick & T;
        catch: null | ((e: unknown) => any);
    }> | Exclude<"clear", keyof T & keyof {
        clear<T_2 extends () => void>(task: T_2): boolean;
        extend<T extends object>(props: T): Pick & T;
        catch: null | ((e: unknown) => any);
    }> | Exclude<"extend", keyof T & keyof {
        clear<T_2 extends () => void>(task: T_2): boolean;
        extend<T extends object>(props: T): Pick & T;
        catch: null | ((e: unknown) => any);
    }> extends infer T_1 extends keyof {
        clear<T_2 extends () => void>(task: T_2): boolean;
        extend<T extends object>(props: T): Pick & T;
        catch: null | ((e: unknown) => any);
    } ? { [P in T_1]: {
        clear<T_2 extends () => void>(task: T_2): boolean;
        extend<T extends object>(props: T): Pick & T;
        catch: null | ((e: unknown) => any);
    }[P]; } : never) & T;
    catch: null | ((e: unknown) => any);
} & {
    raf(cb: () => void): void;
};


//// [DtsFileErrors]


a.d.ts(5,45): error TS2314: Generic type 'Pick' requires 2 type argument(s).
a.d.ts(9,45): error TS2314: Generic type 'Pick' requires 2 type argument(s).
a.d.ts(13,45): error TS2314: Generic type 'Pick' requires 2 type argument(s).
a.d.ts(17,45): error TS2314: Generic type 'Pick' requires 2 type argument(s).
a.d.ts(21,45): error TS2314: Generic type 'Pick' requires 2 type argument(s).


==== fastdom.d.ts (0 errors) ====
    type Omit<T, K extends keyof T> = Pick<T, Exclude<keyof T, K>>;
    
    declare class Fastdom {
      clear<T extends () => void>(task: T): boolean;
      extend<T extends object>(props: T): Omit<this, keyof T & keyof this> & T;
      catch: null | ((e: unknown) => any);
    }
    
    declare const fastdom: Fastdom
    
    export default fastdom;
    
==== a.d.ts (5 errors) ====
    export declare const fastdom: {
        clear: <T extends () => void>(task: T) => boolean;
        extend: <T extends object>(props: T) => (Exclude<"catch", keyof T & keyof {
            clear<T_2 extends () => void>(task: T_2): boolean;
            extend<T extends object>(props: T): Pick & T;
                                                ~~~~
!!! error TS2314: Generic type 'Pick' requires 2 type argument(s).
            catch: null | ((e: unknown) => any);
        }> | Exclude<"clear", keyof T & keyof {
            clear<T_2 extends () => void>(task: T_2): boolean;
            extend<T extends object>(props: T): Pick & T;
                                                ~~~~
!!! error TS2314: Generic type 'Pick' requires 2 type argument(s).
            catch: null | ((e: unknown) => any);
        }> | Exclude<"extend", keyof T & keyof {
            clear<T_2 extends () => void>(task: T_2): boolean;
            extend<T extends object>(props: T): Pick & T;
                                                ~~~~
!!! error TS2314: Generic type 'Pick' requires 2 type argument(s).
            catch: null | ((e: unknown) => any);
        }> extends infer T_1 extends keyof {
            clear<T_2 extends () => void>(task: T_2): boolean;
            extend<T extends object>(props: T): Pick & T;
                                                ~~~~
!!! error TS2314: Generic type 'Pick' requires 2 type argument(s).
            catch: null | ((e: unknown) => any);
        } ? { [P in T_1]: {
            clear<T_2 extends () => void>(task: T_2): boolean;
            extend<T extends object>(props: T): Pick & T;
                                                ~~~~
!!! error TS2314: Generic type 'Pick' requires 2 type argument(s).
            catch: null | ((e: unknown) => any);
        }[P]; } : never) & T;
        catch: null | ((e: unknown) => any);
    } & {
        raf(cb: () => void): void;
    };
    