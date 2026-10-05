interface C {
    a?: boolean;
    [key: string]: null | string | C[keyof C];
}
interface D {
    a?: boolean;
    [key: string]: null | string | KD;
}
type KD = keyof D;
declare const kd: KD;
