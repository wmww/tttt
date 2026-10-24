// origin: npm lightdash@3.52.0 https://github.com/lightdash/lightdash packages/common/tsconfig.bench.json seed 20261304
// flip: yes
// @strict: true
// @target: es2020
export enum TableCalculationType {
    BOOLEAN = 'boolean',
}
export enum DimensionType {
    BOOLEAN = 'boolean',
}
export enum MetricType {
}
export const isBooleanType = (
    type: DimensionType | MetricType | TableCalculationType,
) =>
    type === DimensionType.BOOLEAN ||
    type === TableCalculationType.BOOLEAN;
