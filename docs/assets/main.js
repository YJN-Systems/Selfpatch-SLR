const navToggle = document.querySelector('.nav-toggle');
const siteNav = document.querySelector('#site-nav');

if (navToggle && siteNav) {
  navToggle.addEventListener('click', () => {
    const isOpen = siteNav.classList.toggle('open');
    navToggle.setAttribute('aria-expanded', String(isOpen));
  });

  siteNav.addEventListener('click', event => {
    if (event.target instanceof HTMLAnchorElement) {
      siteNav.classList.remove('open');
      navToggle.setAttribute('aria-expanded', 'false');
    }
  });
}

/*
 * Open navigation dropdowns on hover for devices that actually
 * support hovering. Touch devices retain the normal <details>
 * tap-to-open behavior.
 */
const hoverQuery = window.matchMedia('(hover: hover) and (pointer: fine)');

function configureHoverDropdowns() {
  for (const dropdown of document.querySelectorAll('.nav-dropdown')) {
    if (!(dropdown instanceof HTMLDetailsElement)) {
      continue;
    }

    dropdown.onmouseenter = hoverQuery.matches
      ? () => {
          dropdown.open = true;
        }
      : null;

    dropdown.onmouseleave = hoverQuery.matches
      ? () => {
          dropdown.open = false;
        }
      : null;
  }
}

configureHoverDropdowns();

hoverQuery.addEventListener('change', configureHoverDropdowns);

for (const link of document.querySelectorAll('a[data-external]')) {
  link.setAttribute('target', '_blank');
  link.setAttribute('rel', 'noopener noreferrer');
}

const layout = document.querySelector('.layout');
const tocToggle = document.querySelector('.toc-toggle');

if (layout && tocToggle) {
  tocToggle.addEventListener('click', () => {
    const collapsed = layout.classList.toggle('toc-collapsed');
    tocToggle.setAttribute('aria-expanded', String(!collapsed));
  });
}

const commandTooltip = document.createElement('div');
commandTooltip.className = 'command-tooltip';
commandTooltip.hidden = true;
document.body.appendChild(commandTooltip);

function showCommandTooltip(event, text) {
  if (!text) return;

  commandTooltip.textContent = text;
  commandTooltip.hidden = false;

  const offset = 14;
  commandTooltip.style.left = `${event.clientX + offset}px`;
  commandTooltip.style.top = `${event.clientY + offset}px`;
}

function hideCommandTooltip() {
  commandTooltip.hidden = true;
}

for (const item of document.querySelectorAll('[data-tooltip]')) {
  item.addEventListener('mouseenter', event => {
    showCommandTooltip(event, item.dataset.tooltip);
  });

  item.addEventListener('mousemove', event => {
    showCommandTooltip(event, item.dataset.tooltip);
  });

  item.addEventListener('mouseleave', hideCommandTooltip);

  item.addEventListener('focus', event => {
    const rect = item.getBoundingClientRect();
    showCommandTooltip(
      { clientX: rect.left, clientY: rect.bottom },
      item.dataset.tooltip
    );
  });

  item.addEventListener('blur', hideCommandTooltip);
}
