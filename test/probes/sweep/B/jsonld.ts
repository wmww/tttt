// @strict: true
interface C {
    "@base"?: string | undefined;
    "@flag"?: boolean | undefined;
    [key: string]: null | string | C[keyof C];
}

declare const k: C[keyof C]; declare const s: C[string];
