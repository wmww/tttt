import { EntryPointComponent } from "react-relay/relay-hooks/EntryPointTypes";
type N<T> = T extends EntryPointComponent<infer A, infer B, infer C, infer D> ? [A, B, C, D] : 0;
const x: N<any> = 1 as unknown as string;
