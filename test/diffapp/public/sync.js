const latestResults = {
  tttt: {},
  tsc: {},
}
let serverIsRunningTests = false;
let serverCode = '';
let serverSerial = null;
let codeDiffersFromServer = false;

const inputWaitMs = 50; // How long to wait after an input for more events to batch with it
let inputTimeout;
function onCodeValueChanged(value) {
  clearTimeout(inputTimeout);
  codeDiffersFromServer = true;
  inputTimeout = setTimeout(() => {
    const code = value
    fetch('/setCode?token=' + token, {
      method: 'POST',
      body: code,
    })
      .then(r => r.text())
      .then(t => {
        serverSerial = parseInt(t);
        serverCode = code;
        codeDiffersFromServer = serverCode !== value;
        updateOutOfDateState();
      });
  }, inputWaitMs);
}

function codeChangedByServer(code, serial) {
  serverCode = code;
  serverSerial = serial;
  editor.setValue(code);
  clearTimeout(inputTimeout);
  codeDiffersFromServer = false;
}

function updateOutOfDateState() {
  const outOfDate = (
    serverIsRunningTests ||
    serverSerial !== latestResults.tttt.serial ||
    serverSerial !== latestResults.tsc.serial ||
    codeDiffersFromServer
  );
  codeView.classList.toggle('diff-out-of-date', outOfDate);
  stderrView.classList.toggle('diff-out-of-date', outOfDate);
}

function updateEditor() {
  editor.setAnnotations(
    latestResults.tttt[currentChannel],
    latestResults.tsc[currentChannel],
    serverCode,
    currentChannel == 'err' ? '    ' : null
  );
}

function handleEvents(json) {
  let currentChannelUpdated = false;
  let anyChannelUpdated = false;
  for (const key in json) {
    try {
      const keys = key.split('.');
      switch (keys[0]) {
        case 'errors':
          serverErrors = json[key];
          updateErrors();
          break;

        case 'code':
          if (key == 'code.text') {
            codeChangedByServer(json['code.text'], json['code.serial']);
          }
          break;

        case 'running':
          serverIsRunningTests = json[key];
          updateOutOfDateState();
          break;

        case 'result':
          latestResults[keys[1]][keys[2]] = json[key];
          if (keys[2] == currentChannel || keys[2] == 'serial') {
            currentChannelUpdated = true;
          }
          if (key == 'result.tttt.stderr') {
            setStderrText(json[key]);
          }
          anyChannelUpdated = true;
          break;
      }
    } catch (e) {
      surfaceError('Handling ' + key + ': ' + e.toString());
    }
  }
  const serialsMatch = (
    latestResults.tsc.serial === latestResults.tttt.serial &&
    latestResults.tttt.serial === serverSerial
  );
  if (anyChannelUpdated && serialsMatch) {
    for (const channel of ['err', 'sym', 'typ']) {
      const passed = latestResults.tsc[channel] == latestResults.tttt[channel];
      const button = document.getElementById('channel-button-' + channel);
      button.classList.toggle('fail', !passed);
    }
    updateErrorCounts(latestResults.tttt.err, latestResults.tsc.err);
  }
  if (currentChannelUpdated && serialsMatch) {
    updateOutOfDateState();
    updateEditor();
  }
}

let isBeingUnloaded = false;
addEventListener('beforeunload', () => {
  isBeingUnloaded = true;
});

let currentlyFetching = null; // We should only have one in-flight fetch to /events or /reset at a time

function tryReset() {
  if (currentlyFetching) {
    return;
  }
  currentlyFetching = true;
  fetch('/reset?token=' + token)
    .then(() => {
      currentlyFetching = false;
      fetchEvents();
    }).catch(() => {
      currentlyFetching = false;
      if (connectedToServer) {
        connectedToServer = false;
        updateErrors();
      }
      setTimeout(tryReset, 500);
    });
}

function fetchEvents() {
  // This request may be long-running, assume we are connected at the start rather than showing "disconnected" message
  // while we wait for it to return something. If we're not connected it will fail quickly and this function will not
  // be called again until the /reset endpoint succeeds
  if (!connectedToServer) {
    connectedToServer = true;
    updateErrors();
  }
  if (currentlyFetching) {
    return;
  }
  currentlyFetching = true;
  fetch('/events?token=' + token)
    .then(resp => resp.json())
    .then(json => {
      currentlyFetching = false;
      // console.log('Got message: ', json);
      try {
        handleEvents(json);
        fetchEvents();
      } catch (e) {
        surfaceError(e);
        setTimeout(fetchEvents, 1000);
      }
    }).catch(() => {
      currentlyFetching = false;
      if (!isBeingUnloaded) {
        tryReset();
      }
    });
}

function nativeEditorJumpTo(file, line, char) {
  let url = '/jumpTo?file=' + encodeURIComponent(file);
  if (line !== null) {
    url += '&line=' + encodeURIComponent(line);
  }
  if (char !== null) {
    url += '&char=' + encodeURIComponent(char);
  }
  fetch(url);
}
