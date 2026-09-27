const o = {
  readonly() { return 1; },
  public() { return 2; },
  static: 3,
  async() {},
  get() {},
  declare: 1,
  abstract() {},
  override() {},
  private() {},
  protected: 4,
  accessor() {}
};
class C { readonly() {} static() {} async() {} declare() {} override() {} }
