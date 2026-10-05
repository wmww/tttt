// @strict: true
declare function m<L extends readonly [number, ...number[]]>(...l: L): L;
declare const xs: unknown[];
m(...xs as any);
declare const a: any;
m(...a);
declare function n(...l: [number, ...number[]]): void;
n(...a);
