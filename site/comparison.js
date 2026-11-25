(() => {
  const [table, formats] = document.querySelectorAll('.comparison-table');
  const playerSelect = document.querySelector('#comparison-players');
  const formatSelect = document.querySelector('#format-players');
  const prioritySelect = document.querySelector('#comparison-priority');
  const ranking = document.querySelector('.comparison-ranking');
  const list = ranking.querySelector('ol');
  const summary = ranking.querySelector('.comparison-score-summary');
  const groups = {
    echo: ['gnarlyr', 'stock', 'podium'],
    mini: ['mini', 'echopod'],
    hardware: ['gnarlyr', 'mini', 'hiby', 'q2'],
  };
  const names = new Map([...table.querySelectorAll('thead [data-player]')].map(heading => [
    heading.dataset.player, heading.firstChild.textContent,
  ]));
  const rows = [...table.querySelectorAll('tbody tr')].filter(row => row.querySelector('[data-score]')).map(row => ({
    group: row.parentElement.dataset.group,
    scores: new Map([...row.querySelectorAll('[data-score]')].filter(cell => cell.dataset.score !== 'unknown').map(cell => [
      cell.dataset.player, Number(cell.dataset.score),
    ])),
  }));

  function render() {
    const players = groups[playerSelect.value] ?? [...names.keys()];
    for (const matrix of [table, formats]) {
      for (const cell of matrix.querySelectorAll('[data-player]')) {
        cell.hidden = !players.includes(cell.dataset.player);
      }
      for (const divider of matrix.querySelectorAll('th[scope="rowgroup"]')) {
        divider.colSpan = players.length + 1;
      }
    }

    const shared = rows.filter(row => players.every(player => row.scores.has(player)));
    const weights = shared.map(row => row.group === prioritySelect.value ? 3 : 1);
    const possible = weights.reduce((total, weight) => total + 4 * weight, 0);
    const results = players.map(player => {
      const points = shared.reduce((total, row, i) => total + row.scores.get(player) * weights[i], 0);
      return { player, score: Math.round(100 * points / possible) };
    }).sort((left, right) => right.score - left.score);

    let rank;
    list.replaceChildren(...results.map(({ player, score }, i) => {
      if (i === 0 || score !== results[i - 1].score) rank = i + 1;
      const entry = document.createElement('li');
      entry.value = rank;
      const name = document.createElement('span');
      name.textContent = names.get(player);
      const bar = document.createElement('progress');
      bar.max = 100;
      bar.value = score;
      bar.setAttribute('aria-hidden', 'true');
      const value = document.createElement('strong');
      value.textContent = `${score}/100`;
      entry.append(name, bar, value);
      if (player === 'gnarlyr') entry.classList.add('comparison-highlight');
      return entry;
    }));
    summary.textContent = `${shared.length} of ${rows.length} scored features have a rating for every setup in this view.`;
  }

  for (const select of [playerSelect, formatSelect]) {
    select.addEventListener('change', () => {
      playerSelect.value = formatSelect.value = select.value;
      render();
    });
  }
  prioritySelect.addEventListener('change', render);
  render();
  for (const controls of document.querySelectorAll('.comparison-controls')) controls.hidden = false;
  ranking.hidden = false;
})();
