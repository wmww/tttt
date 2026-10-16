//// [test/cases/ttttJsClassThisMembersInDeclarationOrder.ts] ////

//// [theme.js]
class Theme {
  constructor() {
    this.background = '#fff';
    this.primary = 'a';
  }
  updateColors() {
    this.cScale0 = 'x';
    this.cScale1 = 'y';
  }
  calculate(overrides) {
    this.updateColors();
  }
}
export const getThemeVariables = () => new Theme();
//// [a.ts]
import { getThemeVariables } from './theme.js';
export const build = () => ({ themeVariables: getThemeVariables() });


//// [a.js]
import { getThemeVariables } from './theme.js';
export const build = () => ({ themeVariables: getThemeVariables() });


//// [theme.d.ts]
declare class Theme {
    background: string;
    primary: string;
    cScale0: string | undefined;
    cScale1: string | undefined;
    constructor();
    updateColors(): void;
    calculate(overrides: any): void;
}
export declare const getThemeVariables: () => Theme;
export {};
//// [a.d.ts]
export declare const build: () => {
    themeVariables: {
        background: string;
        primary: string;
        updateColors(): void;
        cScale0: string | undefined;
        cScale1: string | undefined;
        calculate(overrides: any): void;
    };
};
