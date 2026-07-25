document.addEventListener('DOMContentLoaded', function () {
  const article = document.querySelector('.markdown-body');
  const tocRoot = document.getElementById('post-toc');

  if (!article || !tocRoot) {
    return;
  }

  const headings = Array.from(article.querySelectorAll('h2, h3'));
  if (headings.length < 2) {
    tocRoot.style.display = 'none';
    return;
  }

  const seen = new Map();
  const list = document.createElement('ol');
  list.className = 'post-toc-list';

  headings.forEach(function (heading) {
    const base = heading.textContent.trim().toLowerCase()
      .replace(/[\s\u2000-\u206F\u2E00-\u2E7F'"!?.,:;()\[\]{}<>@#%&*+/=~`|^$\\]+/g, '-')
      .replace(/-+/g, '-');

    const slug = seen.has(base)
      ? `${base}-${seen.get(base)}`
      : base;
    seen.set(base, (seen.get(base) || 0) + 1);

    heading.id = slug;

    const item = document.createElement('li');
    item.className = heading.tagName === 'H3' ? 'post-toc-subitem' : 'post-toc-item';
    const link = document.createElement('a');
    link.href = '#' + slug;
    link.textContent = heading.textContent;
    item.appendChild(link);
    list.appendChild(item);
  });

  tocRoot.appendChild(list);
});
