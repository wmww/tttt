//// [test/cases/ttttDeclarationWalkPrintsWithParamNames.ts] ////

//// [m.ts]
export enum ActivityType { Playing, Streaming }
export interface GatewayActivity { name: string; type: ActivityType }
//// [s.ts]
export const kData = Symbol('data');
export type ReplaceOmittedWithUnknown<Omitted extends keyof Data | '', Data> = {
    [Key in keyof Data]: Key extends Omitted ? unknown : Data[Key];
};
export abstract class Structure<DataType extends {}, Omitted extends keyof DataType | '' = ''> {
    protected [kData]: Readonly<ReplaceOmittedWithUnknown<Omitted, DataType>>;
    constructor(data: any) { this[kData] = data; }
}
//// [a.ts]
import type { GatewayActivity, ActivityType } from './m';
import { Structure } from './s';
import { kData } from './s';
export class Activity<Omitted extends keyof GatewayActivity | '' = ''> extends Structure<GatewayActivity, Omitted> {
    /** When type is {@link ActivityType.Streaming} */
    public get type() {
        return this[kData].type;
    }
}


//// [m.js]
export var ActivityType;
(function (ActivityType) {
    ActivityType[ActivityType["Playing"] = 0] = "Playing";
    ActivityType[ActivityType["Streaming"] = 1] = "Streaming";
})(ActivityType || (ActivityType = {}));
//// [s.js]
export const kData = Symbol('data');
export class Structure {
    [kData];
    constructor(data) { this[kData] = data; }
}
//// [a.js]
import { Structure } from './s';
import { kData } from './s';
export class Activity extends Structure {
    /** When type is {@link ActivityType.Streaming} */
    get type() {
        return this[kData].type;
    }
}


//// [m.d.ts]
export declare enum ActivityType {
    Playing = 0,
    Streaming = 1
}
export interface GatewayActivity {
    name: string;
    type: ActivityType;
}
//// [s.d.ts]
export declare const kData: unique symbol;
export type ReplaceOmittedWithUnknown<Omitted extends keyof Data | '', Data> = {
    [Key in keyof Data]: Key extends Omitted ? unknown : Data[Key];
};
export declare abstract class Structure<DataType extends {}, Omitted extends keyof DataType | '' = ''> {
    protected [kData]: Readonly<ReplaceOmittedWithUnknown<Omitted, DataType>>;
    constructor(data: any);
}
//// [a.d.ts]
import type { GatewayActivity } from './m';
import { Structure } from './s';
export declare class Activity<Omitted extends keyof GatewayActivity | '' = ''> extends Structure<GatewayActivity, Omitted> {
    /** When type is {@link ActivityType.Streaming} */
    get type(): "type" extends infer T ? T extends "type" ? T extends Omitted ? unknown : GatewayActivity[T] : never : never;
}
