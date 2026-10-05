// perf: was 1.3 s (the relation maybe stack scanned linearly at 100k entries), tsc 0 errors
interface GeoStream {
    lineEnd(): void;
    lineStart(): void;
    point(x: number, y: number, z?: number): void;
    polygonEnd(): void;
    polygonStart(): void;
    sphere?(): void;
}
interface GeoTransformPrototype {
    lineEnd?(this: this & { stream: GeoStream }): void;
    lineStart?(this: this & { stream: GeoStream }): void;
    point?(this: this & { stream: GeoStream }, x: number, y: number, z?: number): void;
    polygonEnd?(this: this & { stream: GeoStream }): void;
    polygonStart?(this: this & { stream: GeoStream }): void;
    sphere?(this: this & { stream: GeoStream }): void;
}
declare function geoTransform<T extends GeoTransformPrototype>(methods: T): { stream(s: GeoStream): T & GeoStream };
interface CustomTransformProto extends GeoTransformPrototype {
    a: number;
}
declare let customTransformProto: CustomTransformProto;
const t: { stream(s: GeoStream): CustomTransformProto & GeoStream } = geoTransform(customTransformProto);
