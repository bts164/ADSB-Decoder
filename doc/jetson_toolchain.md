# An aarch64 cross toolchain for the Jetson

These steps build a GCC 15 cross compiler for 64-bit Arm on an x86-64 Ubuntu machine, with crosstool-ng. Its
sysroot holds glibc 2.31, the version in Ubuntu 20.04. A binary built with it runs on any aarch64 Linux with
glibc 2.31 or newer.

The toolchain is built once, takes 30 to 60 minutes, and lands in `~/x-tools/aarch64-unknown-linux-gnu`.

| Part | Version | Why |
|---|---|---|
| crosstool-ng | 1.29.0 | the latest release |
| GCC | 15.3 | the same major version as the native build, so adsb compiles the same way |
| glibc | 2.31 | Ubuntu 20.04's; sets the oldest system the binaries run on |
| Linux headers | 5.4 | Ubuntu 20.04's kernel; the Jetson's 5.10 is newer, as it must be |
| CPU | `armv8-a` (the default) | runs on every 64-bit Arm, not only the Orin |

GCC's own runtime libraries (`libstdc++`, `libgcc`, `libgomp`) are built against that glibc 2.31 too. That is the
reason to build the toolchain rather than point a modern cross compiler at an old sysroot: those libraries are
linked statically into adsb, and built against a newer glibc they would need its newer symbols.

## 1. Install crosstool-ng's build dependencies

```sh
sudo apt install gcc g++ gperf bison flex texinfo help2man make libncurses-dev python3-dev \
    autoconf automake libtool libtool-bin gawk wget bzip2 xz-utils unzip patch rsync git meson ninja-build
```

This is the list from crosstool-ng's own Ubuntu Dockerfile, with `libncurses-dev` for `libncurses5-dev`, which
Ubuntu 25.10 no longer has.

## 2. Download and install crosstool-ng

```sh
mkdir -p ~/src/crosstool-ng && cd ~/src/crosstool-ng
wget https://crosstool-ng.org/download/crosstool-ng/crosstool-ng-1.29.0.tar.xz
tar xf crosstool-ng-1.29.0.tar.xz
cd crosstool-ng-1.29.0
./configure --prefix="$HOME/.local/crosstool-ng"
make
make install
export PATH="$HOME/.local/crosstool-ng/bin:$PATH"
ct-ng version
```

Add the `export` line to `~/.bashrc` to keep `ct-ng` on the path. crosstool-ng refuses to run as root, so none of
this uses `sudo`.

## 3. Configure the toolchain

Work in a directory of its own. crosstool-ng writes its `.config` and a large `.build` directory there.

```sh
mkdir -p ~/src/jetson-toolchain && cd ~/src/jetson-toolchain
ct-ng aarch64-unknown-linux-gnu
ct-ng menuconfig
```

The first command loads crosstool-ng's sample for a generic aarch64 Linux, which already enables C++. In
`menuconfig`, change these, and leave everything else as the sample has it:

1. **Operating System → Version of linux:** 5.4.302.
2. **C-library → Version of glibc:** 2.31.
3. **C compiler → Version of gcc:** 15.3.0.
4. **C compiler → Compile libgomp:** on. It's off by default, and adsb's ispc task system runs on OpenMP.
5. **Target options:** leave **Architecture level**, **CPU** and **Tune** empty, for the `armv8-a` baseline.
6. **Debug facilities → gdb:** off, unless you want a cross `gdb` to debug on the Jetson through `gdbserver`.
   It adds build time.

Save and exit. Then check that the choices took:

```sh
grep -E '^CT_(LINUX_VERSION|GLIBC_VERSION|GCC_VERSION|CC_LANG_CXX|CC_GCC_LIBGOMP|ARCH_ARCH|ARCH_CPU)=' .config
```

The output should show Linux 5.4.302, glibc 2.31, GCC 15.3.0, C++ and libgomp on, and empty `ARCH_ARCH` and
`ARCH_CPU`.

Keep a copy of the configuration, so the toolchain can be rebuilt exactly:

```sh
ct-ng savedefconfig    # writes ./defconfig
```

To rebuild from it later: `ct-ng defconfig DEFCONFIG=defconfig`, then step 4.

## 4. Build

```sh
ct-ng build.$(nproc)
```

crosstool-ng downloads every source tarball into `~/src`, builds in `./.build`, and installs into
`~/x-tools/aarch64-unknown-linux-gnu`, which it then makes read-only. `.build` grows to several GB. Once the build
has passed step 5, it can be removed.

!!! note "If glibc fails to compile"
    glibc 2.31 is older than GCC 15, whose warnings are stricter and whose default C standard is C23.
    crosstool-ng turns off `-Werror` for such a pair, so a warning won't stop the build. If glibc still fails with
    a C23 error, such as one about `bool` or `true`, set **C-library → extra target CFLAGS** to `-std=gnu17` and
    run step 4 again. If that doesn't do it, GCC 14 (14.4.0) is the next choice: adsb needs only C++23, which
    GCC 14 has.

## 5. Check the toolchain

```sh
export PATH="$HOME/x-tools/aarch64-unknown-linux-gnu/bin:$PATH"
aarch64-unknown-linux-gnu-g++ --version
```

Build a small C++23 program that uses threads and OpenMP, link its runtime statically as adsb will, and look at
what it needs:

```sh
cd "$(mktemp -d)"
cat > check.cpp <<'EOF'
#include <expected>
#include <print>
#include <thread>
#include <omp.h>
int main() {
    std::expected<int, int> e = 42;
    std::jthread t([] {});
    std::println("{} {}", *e, omp_get_max_threads());
}
EOF
aarch64-unknown-linux-gnu-g++ -std=c++23 -O2 -fopenmp -c check.cpp
aarch64-unknown-linux-gnu-g++ check.o -o check -static-libstdc++ -static-libgcc \
    -Wl,-Bstatic -lgomp -Wl,-Bdynamic -pthread -ldl
file check
aarch64-unknown-linux-gnu-readelf -d check | grep NEEDED
aarch64-unknown-linux-gnu-objdump -T check | grep -o 'GLIBC_[0-9.]*' | sort -Vu | tail -1
```

The link step leaves out `-fopenmp` on purpose: with it, GCC adds its own `-lgomp`, which links the shared
`libgomp` as well. glibc 2.31 still keeps `dlopen` in a separate `libdl`, which the static `libgomp` needs.

What to expect:

- `file` says `ARM aarch64`.
- `NEEDED` lists only glibc's own libraries, such as `libc.so.6`, `libm.so.6` and `ld-linux-aarch64.so.1`:
  no `libstdc++`, `libgcc_s` or `libgomp`.
- The newest `GLIBC_` version is 2.31 or older. That one line is the guarantee that the binary loads on the
  Jetson.

If `qemu-user` is installed (`sudo apt install qemu-user`), the program also runs on this machine:

```sh
qemu-aarch64 -L ~/x-tools/aarch64-unknown-linux-gnu/aarch64-unknown-linux-gnu/sysroot ./check
```

It prints `42` and the thread count. Copy it to the Jetson and run it there too, as the final proof.

## Next

With the toolchain in place, building adsb with it needs a Conan host profile (`arch=armv8`, GCC 15, and the
toolchain's compilers in `[buildenv]`), every dependency built static, and ispc pointed at aarch64 with
`--arch=aarch64 --target-os=linux --target=neon-i32x4`. Those are the next steps.
