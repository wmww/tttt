const mainText = document.getElementById('main-text');
const leftText = document.getElementById('left-text');
const rightText = document.getElementById('right-text');

let currentChannel;
const channelButtons = Array.from(document.getElementsByClassName('channel-button'));
function onChannelSelected(channel) {
  currentChannel = channel;
  channelButtons.forEach(button => {
    button.classList.toggle('active', button.id.endsWith(channel));
  });
  refreshTsc();
}
channelButtons.forEach(button => {
  button.onclick = () => {
    onChannelSelected(button.id.split('-').pop());
  }
});

function refreshCode() {
  fetch('/getCode')
    .then(x => x.text())
    .then(x => {
      mainText.value = x;
    });
}

let mainTextTimeout;
mainText.oninput = () => {
  clearTimeout(mainTextTimeout);
  mainTextTimeout = setTimeout(() => {
    console.log('Setting code');
    fetch('/setCode', {
      method: 'POST',
      body: mainText.value,
    });
  }, 100);
}

let refreshTscTimeout;
function refreshTsc() {
  clearTimeout(refreshTscTimeout);
  fetch('/tsc/' + currentChannel)
    .then(x => x.text())
    .then(x => {
      if (!x) {
        x = '[no output]'
      }
      leftText.value = x;
    })
    .finally(() => {
      refreshTscTimeout = setTimeout(refreshTsc, 100);
    })
}

onChannelSelected('typ');
refreshCode();
