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
    revealLinkedHelp();
