    function revealLinkedHelp(hash = location.hash) {
      let id;
      try { id = decodeURIComponent(hash.slice(1)); } catch { return; }
      const target = document.getElementById(id);
      if (target instanceof HTMLDetailsElement) target.open = true;
    }
    window.addEventListener('hashchange', () => revealLinkedHelp());
    document.addEventListener('click', (event) => {
      const link = event.target instanceof Element ? event.target.closest('a[href^="#"]') : null;
      if (link) revealLinkedHelp(link.hash);
    });

    function commandText(code) {
      const lines = code.querySelectorAll('.cmd');
      return lines.length ? Array.from(lines, (line) => line.textContent).join('\n') : code.textContent;
    }
    document.querySelectorAll('[data-copy]').forEach((button) => {
      let resetTimer;
      let pointerType = '';
      button.addEventListener('pointerdown', (event) => { pointerType = event.pointerType; });
      button.addEventListener('click', async (event) => {
        const pointer = event.detail > 0 ? event.pointerType || pointerType : '';
        pointerType = '';
        const touchInput = pointer
          ? pointer === 'touch' || pointer === 'pen'
          : event.detail > 0 && matchMedia('(hover: none) and (pointer: coarse)').matches;
        const code = document.getElementById(button.dataset.copy);
        const label = button.querySelector('span');
        const status = document.getElementById(button.dataset.copy + '-status');
        const action = button.dataset.copyLabel || button.getAttribute('aria-label');
        button.dataset.copyLabel = action;
        clearTimeout(resetTimer);
        let holdMs = 2500;
        button.disabled = true;
        button.setAttribute('aria-busy', 'true');
        try {
          await navigator.clipboard.writeText(commandText(code));
          label.textContent = 'Copied';
          button.setAttribute('aria-label', action.replace(/^Copy /, 'Copied ') + '. Copy again.');
          status.textContent = 'Copied ' + action.replace(/^Copy /, '') + '.';
        } catch {
          const range = document.createRange();
          range.selectNodeContents(code);
          const selection = window.getSelection();
          selection.removeAllRanges();
          selection.addRange(range);
          label.textContent = 'Selected';
          button.setAttribute('aria-label', action.replace(/^Copy /, 'Selected ') + '. Copy again.');
          const shortcut = /Mac|iPhone|iPad/.test(navigator.platform) ? '⌘C' : 'Ctrl+C';
          status.textContent = touchInput
            ? 'Touch and hold the selected command, then choose Copy.'
            : 'Press ' + shortcut + ' to copy the selected command.';
          holdMs = 8000;
        } finally {
          button.disabled = false;
          button.removeAttribute('aria-busy');
          resetTimer = setTimeout(() => {
            label.textContent = 'Copy';
            button.setAttribute('aria-label', action);
            status.textContent = '';
          }, holdMs);
        }
      });
    });

    /* Interactive screen & app preview decks */
    document.querySelectorAll('[data-deck]').forEach((deck) => {
      const tabs = Array.from(deck.querySelectorAll('[role="tab"]'));
      const panels = Array.from(deck.querySelectorAll('[data-deck-panel]'));
      if (!tabs.length || !panels.length) return;
      const activate = (key, moveFocus) => {
        tabs.forEach((tab) => {
          const selected = tab.dataset.deckTab === key;
          tab.setAttribute('aria-selected', selected ? 'true' : 'false');
          tab.tabIndex = selected ? 0 : -1;
          if (selected && moveFocus) tab.focus();
        });
        panels.forEach((panel) => {
          const active = panel.dataset.deckPanel === key;
          panel.classList.toggle('is-active', active);
          panel.setAttribute('aria-hidden', active ? 'false' : 'true');
          panel.querySelectorAll('a, button').forEach((el) => {
            if (active) el.removeAttribute('tabindex');
            else el.setAttribute('tabindex', '-1');
          });
        });
        const themeHost = deck.closest('.theme-strip');
        if (themeHost) themeHost.dataset.activeTheme = key;
      };
      tabs.forEach((tab, index) => {
        tab.addEventListener('click', () => activate(tab.dataset.deckTab, false));
        tab.addEventListener('keydown', (event) => {
          let next = -1;
          if (event.key === 'ArrowRight') next = (index + 1) % tabs.length;
          else if (event.key === 'ArrowLeft') next = (index - 1 + tabs.length) % tabs.length;
          else if (event.key === 'Home') next = 0;
          else if (event.key === 'End') next = tabs.length - 1;
          if (next >= 0) {
            event.preventDefault();
            activate(tabs[next].dataset.deckTab, true);
          }
        });
      });
    });

    /* Progressive-enhancement lightbox for full-size screenshot links */
    if (typeof HTMLDialogElement === 'function') {
      let dialog;
      let dialogImg;
      let dialogCaption;
      let dialogLink;
      let lastTrigger;
      const ensureLightbox = () => {
        if (dialog) return;
        dialog = document.createElement('dialog');
        dialog.className = 'lightbox';
        dialog.innerHTML =
          '<div class="lightbox-bar">' +
            '<span class="lightbox-caption"></span>' +
            '<div class="lightbox-actions">' +
              '<a class="lightbox-open" target="_blank" rel="noopener">↗</a>' +
              '<button class="lightbox-close" type="button" aria-label="Close">×</button>' +
            '</div>' +
          '</div>' +
          '<div class="lightbox-body"><img alt=""></div>';
        dialogCaption = dialog.querySelector('.lightbox-caption');
        dialogLink = dialog.querySelector('.lightbox-open');
        dialogImg = dialog.querySelector('img');
        dialog.querySelector('.lightbox-close').addEventListener('click', () => dialog.close());
        dialog.addEventListener('click', (event) => {
          if (event.target === dialog) dialog.close();
        });
        dialog.addEventListener('close', () => {
          if (lastTrigger && typeof lastTrigger.focus === 'function') lastTrigger.focus();
        });
        document.body.appendChild(dialog);
      };
      document.addEventListener('click', (event) => {
        if (event.defaultPrevented || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey || event.button !== 0) return;
        const link = event.target instanceof Element ? event.target.closest('a.screen-image, a.desktop-shot') : null;
        if (!link) return;
        const href = link.getAttribute('href') || '';
        if (!/\.(?:png|jpe?g|webp|avif|gif)(?:\?.*)?$/i.test(href)) return;
        const img = link.querySelector('img');
        if (!img) return;
        event.preventDefault();
        ensureLightbox();
        lastTrigger = link;
        dialogImg.src = link.href;
        dialogImg.alt = img.alt || '';
        dialogCaption.textContent = img.alt || '';
        dialogLink.href = link.href;
        dialogLink.setAttribute('aria-label', img.alt || 'Open image');
        dialog.showModal();
      });
    }

    revealLinkedHelp();
