// @strict: true
// The TS7051 of a parameter named like the signature's own type parameter (tsc resolves the name from the parameter)
export type G1 = <T>(T) => void;
export interface G2 { m<U>(U): void }
