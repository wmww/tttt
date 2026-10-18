//// [test/cases/ttttPartialSpreadExactOptionalAnnotation.ts] ////

//// [ttttPartialSpreadExactOptionalAnnotation.ts]
// A partial spread's properties keep their annotations under exactOptionalPropertyTypes, whose missing type the
// printer takes out (vscode's agentHostTelemetryReporter)
export interface Ctx {
    readonly state: 'signedIn' | 'signedOut' | 'unknown';
    readonly week: 'available' | 'unavailable' | 'missing' | 'nonWeekly' | 'stale' | 'expired' | 'invalid';
}
declare function get(c: Ctx | undefined): Ctx | undefined;
declare const b: boolean;
declare const c: Ctx;
export const r = { ...(b ? get(c) : undefined), x: 1 };


//// [ttttPartialSpreadExactOptionalAnnotation.js]
export const r = { ...(b ? get(c) : undefined), x: 1 };


//// [ttttPartialSpreadExactOptionalAnnotation.d.ts]
export interface Ctx {
    readonly state: 'signedIn' | 'signedOut' | 'unknown';
    readonly week: 'available' | 'unavailable' | 'missing' | 'nonWeekly' | 'stale' | 'expired' | 'invalid';
}
export declare const r: {
    state?: 'signedIn' | 'signedOut' | 'unknown';
    week?: 'available' | 'unavailable' | 'missing' | 'nonWeekly' | 'stale' | 'expired' | 'invalid';
    x: number;
};
