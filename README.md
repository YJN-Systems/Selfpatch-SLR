# Selfpatch SLR

Selfpatch SLR (SPSLR) implements runtime **structure layout randomization (SLR)** for C programs on **x86_64 Linux**.

It integrates into the GCC build pipeline and demands little to no changes to the C source code of your project, beyond invoking API functions to patch each program image once. SPSLR consists of two components:

- The `pinpoint` GCC plugin which injects SPSLR metadata into ELF objects during compilation
- The `selfpatch` runtime library which randomizes layouts and patches code and data at startup

This repository additionally includes exemplary executable and shared-library integrations.

This README provides the build instructions for the required custom toolchain and the SPSLR components. It does not explain how to integrate SPSLR into your project. For an extensive **Getting Started** guide on that, information on the internal SPSLR architecture and implementation details, refer to the [documentation website](https://spslr.yjn-systems.com).

---

## Building the Custom GCC Toolchain

The current SPSLR implementation requires minor changes to mainline GCC and the GNU assembler (GAS). This repository provides the corresponding patches at [`toolchain/gcc_16_1_0/component_ref.patch`](https://github.com/YJN-Systems/Selfpatch-SLR/tree/main/toolchain/gcc_16_1_0/component_ref.patch) and [`toolchain/gas_2_46_1/fieldlabel.patch`](https://github.com/YJN-Systems/Selfpatch-SLR/tree/main/toolchain/gas_2_46_1/fieldlabel.patch).

Use the GAS patch to build an assembler that supports the pinpoint plugin's output:

```bash
git clone git://sourceware.org/git/binutils-gdb.git
cd binutils-gdb

git switch -c gas-2.46.1-spslr binutils-2_46_1
git am /path/to/selfpatch-slr/toolchain/gas_2_46_1/fieldlabel.patch

cd ..

mkdir gas-build
cd gas-build

../binutils-gdb/configure \
  --target=x86_64-linux-gnu \
  --program-transform-name='s/^as$/gas-spslr/' \
  --disable-gdb \
  --disable-gprof \
  --disable-sim \
  --disable-gdbserver
make -j$(nproc) all-gas
sudo make install-gas
```

And use the GCC patch to build a compiler that can host pinpoint:

```bash
git clone git://gcc.gnu.org/git/gcc.git
cd gcc

git switch -c gcc-16.1.0-spslr releases/gcc-16.1.0
git am /path/to/selfpatch-slr/toolchain/gcc_16_1_0/component_ref.patch

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
  --disable-libvtv \
  --with-gnu-as \
  --with-as=/usr/local/bin/gas-spslr

make -j$(nproc)
sudo make install

sudo ln -s /usr/local/gcc-spslr/bin/gcc-spslr /usr/local/bin/gcc-spslr
sudo ln -s /usr/local/gcc-spslr/bin/g++-spslr /usr/local/bin/g++-spslr
```

To verify the installations, try:

```bash
gas-spslr --version
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

