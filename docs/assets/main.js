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

const spslrDiagramDetails = {
  'source-a': {
    kicker: 'Host source',
    title: 'a.c',
    text: 'Defines target A and contains a field access that can become an instruction pin.',
    code:
`struct A __attribute__((spslr)) {
    int32_t x;
    int64_t y;
};

int64_t read_a(struct A *a)
{
    return a->y;
}`,
    codeClass: 'language-c'
  },

  'source-b': {
    kicker: 'Host source',
    title: 'b.c',
    text: 'Defines the same target A and contains static storage that becomes a data pin.',
    code:
`struct A __attribute__((spslr)) {
    int32_t x;
    int64_t y;
};

static struct A global_a;`,
    codeClass: 'language-c'
  },

  'source-main': {
    kicker: 'Host source',
    title: 'main.c',
    text: 'Defines a separate target B and provides the host subject entry point. SPSLR is initialized and the host subject image is patched before any pointers into target instances are stored or any target instances are constructed on the heap or stack.',
    code:
`#include "spslr.h"

struct B __attribute__((spslr)) {
    void *ptr;
    int32_t flags;
};

int main(void)
{
    spslr_init();
    spslr_selfpatch();

    // Program logic:
    //   ...
    //   Load modules and use spslr_patch_module(...)
    //   ...
}`,
    codeClass: 'language-c'
  },

  'compile-a': {
    kicker: 'Compile with pinpoint',
    title: 'a.c → a.o + a.c.spslr',
    text: 'Compiling with pinpoint produces the normal object file and one explicit metadata output file. The output path is passed directly to the plugin through the \'out\' argument.',
    code:
`gcc -c a.c -o a.o \\
  -fplugin=pinpoint.so \\
  -fplugin-arg-pinpoint-out=a.c.spslr`,
    codeClass: 'language-bash'
  },

  'compile-b': {
    kicker: 'Compile with pinpoint',
    title: 'b.c → b.o + b.c.spslr',
    text: 'Compiling with pinpoint produces the normal object file and one explicit metadata output file. The output path is passed directly to the plugin through the \'out\' argument.',
    code:
`gcc -c b.c -o b.o \\
  -fplugin=pinpoint.so \\
  -fplugin-arg-pinpoint-out=b.c.spslr`,
    codeClass: 'language-bash'
  },

  'compile-main': {
    kicker: 'Compile with pinpoint',
    title: 'main.c → main.o + main.c.spslr',
    text: 'Compiling with pinpoint produces the normal object file and one explicit metadata output file. The output path is passed directly to the plugin through the \'out\' argument.',
    code:
`gcc -c main.c -o main.o \\
  -fplugin=pinpoint.so \\
  -fplugin-arg-pinpoint-out=main.c.spslr`,
    codeClass: 'language-bash'
  },

  'object-a': {
    kicker: 'Object file',
    title: 'a.o',
    text: 'Compiled and assembled object for a.c. Instruction immediates and static target instances that need patching are marked by symbols. For example, the label spslr_a_ipin0 marks the immediate offset bytes inside the mov instruction responsible for loading A.y.',
    code:
`read_a:
  mov $(spslr_a_ipin0: 0x10), %rax ; The label marks the immediate bytes
  mov (%rdi,%rax,1), %rax
  ret`,
    codeClass: 'language-nasm'
  },

  'object-b': {
    kicker: 'Object file',
    title: 'b.o',
    text: 'Compiled and assembled object for b.c. Instruction immediates and static target instances that need patching are marked by symbols. To make sure that the patchcompile output object can later refer to the address of the global_a variable, an alias symbol with according global visibility is inserted.',
    code:
`.section .data

global_a:
spslr_b_dpin0:
  .long   42            # int32_t a
  .zero   4             # alignment padding
  .quad   0x12345678    # int64_t b`,
    codeClass: 'language-nasm'
  },

  'object-main': {
    kicker: 'Object file',
    title: 'main.o',
    text: 'Compiled and assembled object for main.c. Instruction immediates and static target instances that need patching are marked by symbols.',
  },

  'spslr-a': {
    kicker: 'Metadata file',
    title: 'a.c.spslr',
    text: 'Contains the metadata emitted for a.c (this example shows a shortened form). It describes all targets used in the compilation unit, as well as all inserted instruction and data pins. For example, the 4 byte instruction immediate value labeled \'spslr_a_ipin0\' refers to the target field A.y (8 byte offset into A).',
    code:
`target A local=0 size=16 fields=[
  x: off=0 size=4
  y: off=8 size=8
]
ipin spslr_a_ipin0 target=0 offset=8 width=4`
  },

  'spslr-b': {
    kicker: 'Metadata file',
    title: 'b.c.spslr',
    text: 'Contains the metadata emitted for a.c (this example shows a shortened form). It describes all targets used in the compilation unit, as well as all inserted instruction and data pins. For example, a data pin marks a non-nested instance of target A at a 0 byte offset from the label spslr_b_dpin0 (an alias for global_a).',
    code:
`target A local=0 size=16 fields=[
  x: off=0 size=4
  y: off=8 size=8
]
dpin spslr_b_dpin0 offset=0 target=0 nesting=0`
  },

  'spslr-main': {
    kicker: 'Metadata file',
    title: 'main.c.spslr',
    text: 'Short form of the metadata emitted for main.c.',
    code:
`target B local=0 size=16 fields=[
  ptr: off=0 size=8
  flags: off=8 size=4
]`
  },

  'host-patchcompile': {
    kicker: 'Patchcompile',
    title: 'Host metadata consolidation',
    text: 'Host patchcompile consumes all host .spslr files, emits the descriptor section, and dumps the global target map for later module builds.',
    code:
`patchcompile \\
  --out=host_spslr_section.S \\
  --dump-targets=host.spslr_targets \\
  a.c.spslr b.c.spslr main.c.spslr`,
    codeClass: 'language-bash'
  },

  'host-section': {
    kicker: 'Runtime information',
    title: 'host_spslr_section',
    text: 'Patchcompile generates assembly code containing the host SPSLR runtime descriptors. It must be further assembled to an object file before linking.',
    code:
`as host_spslr_section.S \\
  -o host_spslr_section.o`,
    codeClass: 'language-bash'
  },

  'host-target-map': {
    kicker: 'Target map',
    title: 'host.spslr_targets',
    text: 'The global target map is produced by host patchcompile and reused when compiling module metadata.',
    code:
`global target 0 = struct A
global target 1 = struct B`
  },

  'compile-selfpatch': {
    kicker: 'Runtime component',
    title: 'Selfpatch',
    text: 'Selfpatch provides the runtime implementation for randomizing structure layouts and patching program images in-memory. It should be compiled without pinpoint.',
    code:
`gcc -c selfpatch.c -o selfpatch.o`,
    codeClass: 'language-bash'
  },

  'link-host': {
    kicker: 'Link host subject',
    title: 'Host link step',
    text: 'The host subject links ordinary objects, selfpatch, and the assembled host SPSLR descriptor section.',
    code:
`gcc \\
  a.o b.o main.o \\
  selfpatch.o \\
  host_spslr_section.o \\
  -o host`,
    codeClass: 'language-bash'
  },

  'module-source': {
    kicker: 'Module source',
    title: 'module.c',
    text: 'The module uses target A, which is expected to already exist in the host target map.',
    code:
`struct A __attribute__((spslr)) {
    int32_t x;
    int64_t y;
};

int32_t module_read(struct A *a)
{
    return a->x;
}`,
    codeClass: 'language-c'
  },

  'compile-module': {
    kicker: 'Compile module with pinpoint',
    title: 'module.c → module.o + module.c.spslr',
    text: 'Module sources are also compiled with pinpoint, producing module-local object and metadata files.',
    code:
`gcc -c module.c -o module.o \\
  -fplugin=pinpoint.so \\
  -fplugin-arg-pinpoint-out=module.c.spslr`,
    codeClass: 'language-bash'
  },

  'module-object': {
    kicker: 'Module object',
    title: 'module.o',
    text: 'Ordinary module object code.',
    code:
`module_read:
  mov $(spslr_module_ipin0: 0x00), %rax ; The label marks the immediate bytes
  mov (%rdi,%rax,1), %rax
  ret`,
    codeClass: 'language-bash'
  },

  'module-spslr': {
    kicker: 'Module metadata',
    title: 'module.c.spslr',
    text: 'The module metadata still uses local target IDs until module patchcompile maps them to global target IDs through the host target map.',
    code:
`target A local=0 size=16 fields=[
  x: off=0 size=4
  y: off=8 size=8
]
ipin spslr_module_ipin0 target=0 offset=0 width=4`
  },

  'module-patchcompile': {
    kicker: 'Module patchcompile',
    title: 'Module metadata consolidation',
    text: 'Module patchcompile loads the host target map and rejects targets not already known to the host. All target IDs in instruction and data pins are mapped to the global target IDs defined by the host target mapping. No runtime target information is generated for modules.',
    code:
`patchcompile \\
  --module \\
  --load-targets=host.spslr_targets \\
  --no-new-targets \\
  --out=module_spslr_section.S \\
  module.c.spslr`,
    codeClass: 'language-bash'
  },

  'module-section': {
    kicker: 'Runtime information',
    title: 'module_spslr_section',
    text: 'The module patchcompile assembly output contains the runtime ipin and dpin descriptors. It must be further assembled to an object file before linking.',
    code:
`as module_spslr_section.S \\
  -o module_spslr_section.o`,
    codeClass: 'language-bash'
  },

  'link-module': {
    kicker: 'Link module subject',
    title: 'Module link step',
    text: 'The module image links its object code and reduced SPSLR descriptor section. Selfpatch lives only in host subjects, so it is not linked here.',
    code:
`gcc -shared \\
  module.o \\
  module_spslr_section.o \\
  -o module.so`,
    codeClass: 'language-bash'
  },

};

for (const diagram of document.querySelectorAll('.spslr-diagram')) {
  const wrap = diagram.closest('.spslr-diagram-wrap');
  const detail = wrap?.querySelector('.spslr-diagram-detail');
  const items = diagram.querySelectorAll('[data-detail]');

  function showDiagramDetail(key) {
    const item = spslrDiagramDetails[key];
    if (!item || !detail) return;

    for (const candidate of items) {
      candidate.classList.toggle('is-active', candidate.dataset.detail === key);
    }

    const codeClassHtml = item.codeClass
      ? `class="${escapeHtml(item.codeClass)}"`
      : '';

    const codeHtml = item.code
      ? `<pre><code ${codeClassHtml}>${escapeHtml(item.code)}</code></pre>`
      : '';

    detail.innerHTML = `
      <p class="detail-kicker">${escapeHtml(item.kicker)}</p>
      <h4>${escapeHtml(item.title)}</h4>
      <p>${escapeHtml(item.text)}</p>
      ${codeHtml}
    `;

    /* Re-run Prism on dynamically inserted code blocks. */
    const codeElement = detail.querySelector('code');

    if (codeElement && window.Prism) {
      Prism.highlightElement(codeElement);
    }
  }

  for (const item of items) {
    item.addEventListener('mouseenter', () => {
      const key = item.dataset.detail;

      for (const candidate of items) {
        candidate.classList.toggle(
          'is-hovered',
          candidate.dataset.detail === key
        );
      }
    });

    item.addEventListener('mouseleave', () => {
      for (const candidate of items) {
        candidate.classList.remove('is-hovered');
      }
    });

    item.addEventListener('focus', () => {
      const key = item.dataset.detail;

      for (const candidate of items) {
        candidate.classList.toggle(
          'is-hovered',
          candidate.dataset.detail === key
        );
      }
    });

    item.addEventListener('blur', () => {
      for (const candidate of items) {
        candidate.classList.remove('is-hovered');
      }
    });

    item.addEventListener('click', () => {
      showDiagramDetail(item.dataset.detail);
    });
  }
}

function escapeHtml(value) {
  return value
    .replaceAll('&', '&amp;')
    .replaceAll('<', '&lt;')
    .replaceAll('>', '&gt;')
    .replaceAll('"', '&quot;')
    .replaceAll("'", '&#039;');
}
