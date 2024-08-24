class Editor {
  constructor(inputElem, viewElem) {
    inputElem.value = '';

    this.inputElem = inputElem;
    this.viewElem = viewElem;
    this.annotationA = null;
    this.annotationB = null;
    this.referenceValue = '';
    this.selectionAnchor = null;
    this.selectionFocus = null;

    document.addEventListener('selectionchange', () => {
      if (document.activeElement == inputElem) {
        this.render();
      }
    });

    this.inputElem.addEventListener('focusout', () => this.render());

    const selectTo = ev => {
      let offset = this.tokenOffsetAtPoint(ev.clientX, ev.clientY);
      if (offset === null) {
        offset = 0;
      }
      this.selectionFocus = offset;
      if (this.selectionAnchor === null) {
        this.selectionAnchor = offset;
      }
      let direction = 'forward';
      let start = this.selectionAnchor;
      let end = this.selectionFocus;
      if (this.selectionFocus < this.selectionAnchor) {
        direction = 'backward';
        end = this.selectionAnchor;
        start = this.selectionFocus;
      }
      this.inputElem.focus({preventScroll: true});
      this.inputElem.setSelectionRange(start, end, direction);
    }

    this.viewElem.addEventListener('mousedown', ev => {
      selectTo(ev);
      this.viewElem.addEventListener('mousemove', selectTo);
    });

    this.viewElem.addEventListener('mouseup', ev => {
      this.viewElem.removeEventListener('mousemove', selectTo);
      selectTo(ev);
      this.selectionAnchor = null;
      this.selectionFocus = null;
    });

    this.render();
  }

  tokenOffsetAtPoint(x, y) {
    let range;

    if (document.caretRangeFromPoint) {
      // Chromiums seem to like this one
      range = document.caretRangeFromPoint(x, y);
    } else if (document.caretPositionFromPoint) {
      // Geckos seem to like this one
      const position = document.caretPositionFromPoint(x, y);
      range = document.createRange();
      try {
        range.setStart(position.offsetNode, position.offset);
      } catch {
        return null;
      }
    }
    else {
      // Unsupported browser
      return null;
    }

    if (range.startContainer.tokenOffset !== undefined) {
      return range.startContainer.tokenOffset + range.startOffset;
    } else if (range.startContainer.parentElement.tokenOffset !== undefined) {
      return range.startContainer.parentElement.tokenOffset + range.startOffset;
    } else {
      return null;
    }
  }

  setValue(value) {
    this.inputElem.value = value;
    this.render();
  }

  tokenizeValue(indent) {
    const rawValue = this.inputElem.value;
    const selectionStart = this.inputElem.selectionStart;
    const selectionEnd = this.inputElem.selectionEnd;
    const selectionForward = this.inputElem.selectionDirection !== 'backward';
    const beforeSelection = rawValue.slice(0, selectionStart);
    const selection = rawValue.slice(selectionStart, selectionEnd);
    const afterSelection = rawValue.slice(selectionEnd);
    const cursorToken = {
      type: Token.CURSOR,
      offset: selectionForward ? selectionEnd : selectionStart,
    };
    const tokens = [];
    addLineStartTokensTo(true, indent, tokens);
    tokenizeStringInto(beforeSelection, 0, undefined, true, indent, tokens);
    if (!selectionForward && document.activeElement == this.inputElem) {
      tokens.push(cursorToken);
    }
    tokenizeStringInto(selection, selectionStart, 'token-selected', true, indent, tokens);
    if (selectionForward && document.activeElement == this.inputElem) {
      tokens.push(cursorToken);
    }
    tokenizeStringInto(afterSelection, selectionEnd, undefined, true, indent, tokens);
    tokens.push({
      type: Token.ENDLINE,
      offset: rawValue.length,
    });
    return tokens;
  }

  render() {
    const tokens = this.tokenizeValue(null);
    renderTokensTo(this.viewElem, tokens);
  }
}
