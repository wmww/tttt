// @declaration: true
// @allowJs: true
// @target: es2022
// A JavaScript class's `this.x = v` members go among its methods by where they're declared (tsgo's getNamedMembers
// sorts a class's own members by declaration: mermaid's theme classes printed through a .ts file)
// @filename: theme.js
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
// @filename: a.ts
import { getThemeVariables } from './theme.js';
export const build = () => ({ themeVariables: getThemeVariables() });
