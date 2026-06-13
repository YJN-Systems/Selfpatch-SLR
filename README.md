# Selfpatch SLR

Selfpatch SLR (SPSLR) implements runtime **structure layout randomization (SLR)** for C programs on **x86_64 Linux**.

It integrates into the GCC build pipeline and demands little to no changes to the C source code of your project. This repository includes the new pipeline components, as well as exemplary executable and shared-module integrations.

For information on the internal SPSLR architecture and implementation details, refer to the [documentation website](https://spslr.yjn-systems.com).

---

## Status

SPSLR is a **research prototype** intended for experimentation and evaluation.

Current support:

- x86\_64 Linux
- GCC 16 (custom toolchain)
- C projects

---

## Requirements

### Platform

- x86\_64 Linux

### Toolchain

- custom `gcc-16`
- custom `g++-16`
- GCC plugin support
- CMake

---

## Building the Custom GCC Toolchain

The current SPSLR implementation requires minor changes to mainline GCC. This repository provides the corresponding patch at [`gcc_patches/gcc16/gcc_component_ref.patch`](https://github.com/YJN-Systems/Selfpatch-SLR/tree/main/gcc_patches/gcc16/gcc_component_ref.patch). Use this patch file to build a GCC version compatible with SPSLR:

```bash
git clone git://gcc.gnu.org/git/gcc.git
cd gcc

git switch -c gcc-16-spslr releases/gcc-16.1.0
git am /path/to/selfpatch-slr/gcc_component_ref.patch

cd ..

mkdir gcc-build
cd gcc-build

../gcc/configure \
  --enable-host-shared \
  --prefix=/usr/local/gcc-spslr \
  --program-suffix=-spslr \
  --enable-languages=c,c++ \
  --enable-plugin \
  --disable-multilib \
  --disable-werror \
  --disable-bootstrap \
  --disable-libsanitizer \
  --disable-libquadmath \
  --disable-libvtv

make -j$(nproc)
sudo make install

sudo ln -s /usr/local/gcc-spslr/bin/gcc-spslr /usr/local/bin/gcc-spslr
sudo ln -s /usr/local/gcc-spslr/bin/g++-spslr /usr/local/bin/g++-spslr
```

To verify the installation, try:

```bash
gcc-spslr --version
g++-spslr --version
```

---

## Building SPSLR

To build all SPSLR targets of this repository, use:

```bash
cmake -S . -B build \
  -DCMAKE_C_COMPILER=gcc-spslr \
  -DCMAKE_CXX_COMPILER=g++-spslr

cmake --build build -j$(nproc)
```

This generates the core SPSLR components:
- `pinpoint`
- `patchcompile`
- `selfpatch`

As well as the example subject and module: 
- `subject`
- `module.so`

---

## Quick Start

The repository includes a working integration example in [`subject/`](https://github.com/YJN-Systems/Selfpatch-SLR/tree/main/subject).

It follows the following steps:

1. Compile sources using `pinpoint`
2. Run `patchcompile` on the pinpoint metadata
3. Assemble the generated descriptor assembly into an object file
4. Link the runtime library and descriptor object with your program objects
5. Call `spslr_init()` and `spslr_selfpatch()` during startup

---

## `pinpoint`

Use the pinpoint GCC plugin to compile your program sources.

### Plugin arguments

| Argument | Description |
|---|---|
| `out=<file>` | Output metadata file |
| `verbose` | Enable verbose logging |
| `metadir=<dir>` | Legacy mode: metadata output directory |
| `srcroot=<dir>` | Legacy mode: source-tree root used to derive source-relative metadata paths |

`out=<file>` is the preferred explicit output mode. For build systems where passing a per-translation-unit output path is inconvenient, `pinpoint` also supports the legacy `metadir=<dir>` and `srcroot=<dir>` arguments. In this mode, the plugin derives the output path from the compiled source file: a source file `<srcroot>/path/to/file.c` emits metadata to `<metadir>/path/to/file.c.spslr`. Use either `out=<file>` or both `metadir=<dir>` and `srcroot=<dir>`.

### Example

```bash
gcc-spslr \
  -O1 \
  -fplugin=/path/to/pinpoint.so \
  -fplugin-arg-pinpoint-out=build/spslr/main.c.spslr \
  -fplugin-arg-pinpoint-verbose \
  -c main.c \
  -o build/main.o
```

Each translation unit should emit its own `.spslr` file.

---

## `patchcompile`

Accumulate the generated metadata of all compilation units and generate corresponding SPSLR runtime information using the patchcompile command line tool.

### Usage

```bash
patchcompile --out=<file> [options] <file> ...
```

### Options

| Option | Description |
|---|---|
| `--help` | Show usage |
| `--verbose` | Enable verbose logging |
| `--out=<file>` | Output descriptor assembly |
| `--load-targets=<file>` | Load target map |
| `--dump-targets=<file>` | Write target map |
| `--no-new-targets` | Reject unknown targets |
| `--module` | Generate module descriptor data |

### Executable example

When building a standalone executable, specify the `--out=<file>` option to define where the compiled runtime information assembly should be dumped. If the binary can dynamically load modular extensions that use randomized structures, also specify `--dump-targets=<file>` to generate a global target namespace.

```bash
patchcompile \
  --out=build/subject_spslr_section.S \
  --dump-targets=build/subject.spslr_targets \
  --verbose \
  build/spslr/main.c.spslr
```

### Shared module example

Modules do not own target randomization states. Instead, they are randomized by the executable that loads them. To ensure compatibility with the host binary, supply its target mapping via `--load-targets=<file>` and prevent the module code from introducing new structures for randomization using `--no-new-targets`. Lastly, specify `--module` to make patchcompile omit runtime information that is owned exclusively by the hosting program.

```bash
patchcompile \
  --out=build/spslr_module_section.S \
  --load-targets=build/subject.spslr_targets \
  --no-new-targets \
  --module \
  --verbose \
  build/spslr_module/module.c.spslr
```

---

## Runtime API

To access the runtime SPSLR API, include the selfpatch header:

```c
#include <spslr.h>
```

The four public entry points are:

```c
struct spslr_status spslr_init(void);
struct spslr_status spslr_selfpatch(void);
struct spslr_status spslr_patch_module(
    const struct spslr_module *m,
    void *reorder_buffer;
);
unsigned long spslr_reorder_buffer_size(void);
```

### `spslr_init()`

Should be called first. It decides the randomized target structure layouts but does no patching yet.

```c
struct spslr_status st = spslr_init();
if (st.error != SPSLR_OK) err;
```

### `spslr_selfpatch()`

Patches the main executable. It must be called before any instances of target structures are dynamically allocated on the stack or heap and before pointers to target struct fields are stored. If unsure at what point in your program this is the case, call it in the very beginning of `main`, but do not forget the preceding call to `spslr_init`.

```c
struct spslr_status st = spslr_selfpatch();
if (st.error != SPSLR_OK) err;
```

### `spslr_patch_module() and spslr_reorder_buffer_size()`

Patches a shared module on load with the structure layouts owned by the main executable. It takes a collection of pointers to the relevant SPSLR runtime information constructs in the module, which can be located through the symbols defined in the selfpatch header. Patching a module that has not yet had its relocations applied is invalid (only relevant to custom module loading mechanisms). Additionally, a temporary buffer of at least `spslr_reorder_buffer_size()` must be supplied and may be freed after module patching completes.

```c
#include <dlfcn.h>
#include <spslr.h>

int patch_loaded_module(void *handle)
{
    struct spslr_module m = {
        .ipin_cnt =
            dlsym(handle, SPSLR_MODULE_SYM_IPIN_CNT),
        .ipins =
            dlsym(handle, SPSLR_MODULE_SYM_IPINS),
        .ipin_op_cnt =
            dlsym(handle, SPSLR_MODULE_SYM_IPIN_OP_CNT),
        .ipin_ops =
            dlsym(handle, SPSLR_MODULE_SYM_IPIN_OPS),
        .dpin_cnt =
            dlsym(handle, SPSLR_MODULE_SYM_DPIN_CNT),
        .dpins =
            dlsym(handle, SPSLR_MODULE_SYM_DPINS)
    };

    void *reorder_buffer = malloc(spslr_reorder_buffer_size());

    struct spslr_status st =
        spslr_patch_module(&m, reorder_buffer);

    return st.error != SPSLR_OK;
}
```

### Viability

Besides the error code, the `spslr_status.viability` field describes the viability of an image after an SPSLR function has run. If a function returned an `SPSLR_OK` error code, the image has been fully randomized and is guaranteed to be viable.

Should the `spslr_selfpatch` fail before patching starts, a non-OK error code is returned but the program may still be safe to run with its original layouts. If it fails after only parts of the image have been patched, the program is no longer viable and must be terminated immediately.

Modular patching results are only considered viable if the full patch has been applied. If, for example, no SPSLR runtime information can be found in the module, it can not safely be adjusted to match the randomized host binary layouts. Should `spslr_patch_module` indicate a non-OK and thus non-viable result, the module code must be unloaded and never run.

---

## Limitations

Structure layout randomization fundamentally conflicts with assumptions made by the C language and common low-level programming idioms.

In standard C, structure layout is fixed and observable. Structure layout randomization (of any kind) intentionally violates these assumptions by randomizing field layouts at runtime. As a result, some otherwise-valid code constructs are incompatible with SLR.

Examples include:

- casting a structure pointer to a pointer to its first member
- assumptions about field ordering or contiguous placement
- serialization or ABI logic that depends on a stable in-memory layout

In general, code must treat structure layout as opaque to be compatible.

This conceptual limitation is inherent to SLR itself and not specific to SPSLR.

Currently, SPSLR has additional implementation-specific limitations and constraints. These are documented separately on the [documentation website](https://spslr.yjn-systems.com) and may or may not be fixed in the future.

---


