# Selfpatch SLR

Selfpatch SLR (SPSLR) implements runtime **structure layout randomization (SLR)** for C programs on **x86_64 Linux**.

It integrates into the GCC build pipeline and demands little to no changes to the C source code of your project, beyond invoking API functions to patch each program image once. SPSLR consists of two components:

- The `pinpoint` GCC plugin which injects SPSLR metadata into ELF objects during compilation
- The `selfpatch` runtime library which randomizes layouts and patches code and data at startup

This repository additionally includes exemplary executable and shared-library integrations.

This README provides the build instructions for the required custom toolchain and the SPSLR components. It does not explain how to integrate SPSLR into your project. For an extensive **Getting Started** guide on that, information on the internal SPSLR architecture and implementation details, refer to the [documentation website](https://spslr.yjn-systems.com).

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
- `selfpatch`

As well as the example subject and module: 
- `subject`
- `module.so`

---

