//// [test/cases/ttttDeclarationPaintedMergedDeclarations.ts] ////

//// [l.ts]
export class URI { static isUri(x: any): x is URI { return true; } }
export interface Item { label: string }
//// [c.ts]
import { URI, Item } from "./l";
namespace Pick {
    export function is(thing: object | undefined): thing is Pick { return !!thing && URI.isUri((<Pick>thing).filepath); }
}
interface Pick extends Item { filepath: URI; }
export function promote(existing: Pick[]): void {}
//// [d.ts]
import { Item } from "./l";
interface Provider { item: Item }
declare function createDecorator<T>(name: string): { _: T };
const IService = createDecorator<IService>("s");
interface IService { register(provider: Provider): void; }
export class Main { constructor(service: IService) {} }


//// [l.js]
export class URI {
    static isUri(x) { return true; }
}
//// [c.js]
import { URI } from "./l";
var Pick;
(function (Pick) {
    function is(thing) { return !!thing && URI.isUri(thing.filepath); }
    Pick.is = is;
})(Pick || (Pick = {}));
export function promote(existing) { }
//// [d.js]
const IService = createDecorator("s");
export class Main {
    constructor(service) { }
}


//// [l.d.ts]
export declare class URI {
    static isUri(x: any): x is URI;
}
export interface Item {
    label: string;
}
//// [c.d.ts]
import { URI, Item } from "./l";
declare namespace Pick {
    function is(thing: object | undefined): thing is Pick;
}
interface Pick extends Item {
    filepath: URI;
}
export declare function promote(existing: Pick[]): void;
export {};
//// [d.d.ts]
import { Item } from "./l";
interface Provider {
    item: Item;
}
declare const IService: {
    _: IService;
};
interface IService {
    register(provider: Provider): void;
}
export declare class Main {
    constructor(service: IService);
}
export {};
