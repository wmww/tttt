type P1 = ((s: string) => s is "a") extends (arg: any) => arg is infer R ? R : 0;
type P2 = ((s: string) => s is "a") extends (arg: any) => infer R ? R : 0;
type P3 = ((s: string) => s is "a") extends (arg: any) => arg is "a" ? 1 : 0;
type P4 = ((s: string) => s is "a") extends (arg: string) => arg is infer R ? R : 0;
const a: [P1, P2, P3, P4] = null!;
const b: 5 = a;
