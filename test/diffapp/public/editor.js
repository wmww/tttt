const doubleClickThresh = 400;
const wordCharRe = /\w/;

class Editor {
  constructor(inputElem, viewElem) {
    inputElem.value = '';

    this.inputElem = inputElem;
    this.viewElem = viewElem;
    this.selectionAnchor = null;
    this.selectionFocus = null;
    this.lastMousedownTime = null;
    this.lastMousedownClientX = null
    this.lastMousedownClientY = null;
    this.selectByWord = false;

    this.previousBuffers = [];
    this.nextBuffers = []; // NOTE: this array is backwards, 0 is the right-most element, length-1 is the left most

    this.onBuffersChanged = () => {};
    this.onValueChanged = () => {};

    document.addEventListener('selectionchange', () => {
      if (document.activeElement == inputElem) {
        this.render();
      }
    });

    inputElem.addEventListener('input', () => {
      this.onValueChanged(this.inputElem.value);
      if (this.nextBuffers.length) {
        while (this.nextBuffers.length) {
          this.previousBuffers.push(this.nextBuffers.pop());
        }
        this.onBuffersChanged(this.previousBuffers.length, this.nextBuffers.length);
      }
    });

    this.inputElem.addEventListener('focusout', () => {
      // Re-rendering prevents buttons in the editor from being clicked (they are destroyed and recreated in the frame
      // before the click event can go through). This simulates the only change that would result from re-rendering.
      document.querySelectorAll('.token-cursor').forEach(elem => {
        elem.style.display = 'none';
      })
    });

    const selectTo = ev => {
      let offset = this.tokenOffsetAtPoint(ev.clientX, ev.clientY);
      if (offset === null) {
        let beginning = false;
        if (this.viewElem.firstElementChild) {
          const top = this.viewElem.firstElementChild.getBoundingClientRect().top;
          const bottom = this.viewElem.lastElementChild.getBoundingClientRect().bottom;
          const middle = (top + bottom) / 2;
          beginning = ev.clientY < middle;
        }
        offset = beginning ? 0 : this.inputElem.value.length;
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
      if (this.selectByWord) {
        const value = this.inputElem.value;
        while (start > 0 && wordCharRe.test(value[start - 1])) {
          start--;
        }
        while (end < value.length && wordCharRe.test(value[end])) {
          end++;
        }
      }
      this.inputElem.setSelectionRange(start, end, direction);
    }

    this.viewElem.addEventListener('mousedown', ev => {
      if (ev.target.onclick) {
        return;
      }
      const time = performance.now();
      if (
        this.lastMousedownTime &&
        time - this.lastMousedownTime < doubleClickThresh &&
        ev.clientX === this.lastMousedownClientX &&
        ev.clientY === this.lastMousedownClientY
      ) {
        this.selectByWord = true;
      }
      this.lastMousedownTime = time;
      this.lastMousedownClientX = ev.clientX;
      this.lastMousedownClientY = ev.clientY;
      if (ev.shiftKey) {
        this.selectionAnchor = (
          this.inputElem.selectionDirection === 'backward' ?
          this.inputElem.selectionEnd :
          this.inputElem.selectionStart);
      }
      selectTo(ev);
      this.viewElem.addEventListener('mousemove', selectTo);
      this.render();
    });

    this.viewElem.addEventListener('mouseup', ev => {
      this.viewElem.removeEventListener('mousemove', selectTo);
      selectTo(ev);
      this.selectionAnchor = null;
      this.selectionFocus = null;
      this.selectByWord = false;
    });
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

    let editorStartOfElem = undefined;
    let editorEndOfElem = undefined;
    if (range.startContainer.editorStart !== undefined) {
      editorStartOfElem = range.startContainer.editorStart;
      editorEndOfElem = range.startContainer.editorEnd;
    } else if (range.startContainer.parentElement.editorStart !== undefined) {
      editorStartOfElem = range.startContainer.parentElement.editorStart;
      editorEndOfElem = range.startContainer.parentElement.editorEnd;
    }

    if (editorStartOfElem === undefined) {
      return null;
    } else {
      let offset = editorStartOfElem + range.startOffset;
      if (offset > editorEndOfElem) {
        offset = editorEndOfElem;
      }
      return offset;
    }
  }

  setValue(value) {
    if (value !== this.inputElem.value) {
      if (this.inputElem.value !== '') {
        this.previousBuffers.push(this.inputElem.value);
      }
      this.inputElem.value = value;
      this.render();
      this.onValueChanged(value);
      this.onBuffersChanged(this.previousBuffers.length, this.nextBuffers.length);
    }
  }

  // -1 for back, 1 for forward, etc
  changeBuffer(relative) {
    let buffer = this.inputElem.value;
    while (relative < 0) {
      // nextBuffers is backwards to make this easy
      if (buffer !== '') {
        this.nextBuffers.push(buffer);
      }
      buffer = this.previousBuffers.pop();
      if (buffer === undefined) {
        buffer = '';
      }
      relative++;
    }
    while (relative > 0) {
      if (buffer !== '') {
        this.previousBuffers.push(buffer);
      }
      buffer = this.nextBuffers.pop();
      if (buffer === undefined) {
        buffer = '';
      }
      relative--;
    }
    this.inputElem.value = buffer;
    this.render();
    this.onValueChanged(buffer);
    this.onBuffersChanged(this.previousBuffers.length, this.nextBuffers.length);
  }

  tokenizeValue(indent) {
    const rawValue = this.inputElem.value;
    const selectionStart = this.inputElem.selectionStart;
    const selectionEnd = this.inputElem.selectionEnd;
    const selectionForward = this.inputElem.selectionDirection !== 'backward';
    const beforeSelection = rawValue.slice(0, selectionStart);
    const selection = rawValue.slice(selectionStart, selectionEnd);
    const afterSelection = rawValue.slice(selectionEnd);
    const cursorPosition = selectionForward ? selectionEnd : selectionStart;
    const tokens = [];
    addLineStartTokensTo(tokens, {
      indent: indent,
      lineNumbers: true,
      editorStart: 0,
      editorEnd: 0,
    });
    tokenizeStringInto(beforeSelection, tokens, {
      lineNumbers: true,
      indent: indent,
      editorStart: 0,
      editorEnd: beforeSelection.length,
    });
    if (!selectionForward && document.activeElement == this.inputElem) {
      tokens.push(makeCursorToken(cursorPosition));
    }
    tokenizeStringInto(selection, tokens, {
      lineNumbers: true,
      indent: indent,
      editorStart: selectionStart,
      editorEnd: selectionEnd,
      classes: ['token-selected'],
    });
    if (selectionForward && document.activeElement == this.inputElem) {
      tokens.push(makeCursorToken(cursorPosition));
    }
    const valueLen = rawValue.length;
    tokenizeStringInto(afterSelection, tokens, {
      lineNumbers: true,
      indent: indent,
      editorStart: selectionEnd,
      editorEnd: valueLen,
    });
    addEofTokensTo(tokens, {
      editorStart: valueLen,
      editorEnd: valueLen
    });
    return tokens;
  }

  render() {
    const tokens = this.tokenizeValue(null);
    renderTokensTo(this.viewElem, tokens);
  }
}
