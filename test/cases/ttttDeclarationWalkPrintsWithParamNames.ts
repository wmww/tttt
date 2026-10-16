// @declaration: true
// @strict: true
// @target: es2022
// The declaration walk prints an inferred type with the type parameter names in scope, like the emit does: printed
// without them, the getter's deferred conditional (`"type" extends infer T ? ...`) came out resolved to
// `ActivityType`, painting an import the emitted text never names (discord.js structures' Activity.ts)
// @filename: m.ts
export enum ActivityType { Playing, Streaming }
export interface GatewayActivity { name: string; type: ActivityType }
// @filename: s.ts
export const kData = Symbol('data');
export type ReplaceOmittedWithUnknown<Omitted extends keyof Data | '', Data> = {
    [Key in keyof Data]: Key extends Omitted ? unknown : Data[Key];
};
export abstract class Structure<DataType extends {}, Omitted extends keyof DataType | '' = ''> {
    protected [kData]: Readonly<ReplaceOmittedWithUnknown<Omitted, DataType>>;
    constructor(data: any) { this[kData] = data; }
}
// @filename: a.ts
import type { GatewayActivity, ActivityType } from './m';
import { Structure } from './s';
import { kData } from './s';
export class Activity<Omitted extends keyof GatewayActivity | '' = ''> extends Structure<GatewayActivity, Omitted> {
    /** When type is {@link ActivityType.Streaming} */
    public get type() {
        return this[kData].type;
    }
}
