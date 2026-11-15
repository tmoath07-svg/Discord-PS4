(() => {
  const channelName = document.getElementById('channel-name');
  const input = document.getElementById('message-input');
  const messages = document.getElementById('messages');
  const form = document.getElementById('composer');
  const status = document.getElementById('status');

  document.querySelectorAll('.channel').forEach(button => {
    button.addEventListener('click', () => {
      document.querySelectorAll('.channel').forEach(item => item.classList.remove('selected'));
      button.classList.add('selected');
      channelName.textContent = button.dataset.channel;
      input.placeholder = 'Message #' + button.dataset.channel;
      status.textContent = 'Offline preview · #' + button.dataset.channel;
    });
  });

  form.addEventListener('submit', event => {
    event.preventDefault();
    const value = input.value.trim();
    if (!value) return;
    const article = document.createElement('article');
    article.className = 'message';
    const avatar = document.createElement('div');
    avatar.className = 'message-avatar';
    avatar.textContent = 'P';
    const body = document.createElement('div');
    const meta = document.createElement('div');
    meta.className = 'message-meta';
    const author = document.createElement('strong');
    author.textContent = 'Player (local)';
    const time = document.createElement('time');
    time.textContent = new Date().toLocaleTimeString([], {hour:'2-digit', minute:'2-digit'});
    const paragraph = document.createElement('p');
    paragraph.textContent = value;
    meta.append(author, time);
    body.append(meta, paragraph);
    article.append(avatar, body);
    messages.append(article);
    input.value = '';
    messages.scrollTop = messages.scrollHeight;
  });

  // Basic keyboard/gamepad focus support. Native browser focus handles D-pad navigation.
  document.addEventListener('keydown', event => {
    if (event.key === 'Escape') document.activeElement.blur();
    if (event.key === 'Enter' && document.activeElement === document.body) input.focus();
  });

  window.addEventListener('gamepadconnected', () => {
    status.textContent = 'Controller connected · Offline preview';
  });
  window.addEventListener('gamepaddisconnected', () => {
    status.textContent = 'Offline preview · Not connected to Discord';
  });
})();