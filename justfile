default: build

# training run whose profile steers inlining and branch layout of the PGO build
pgo_training := "--soc sti5512 --board dsr9500 --firmware firmware/dsr9500mcmain.bin --run 200000000"
llvm_profdata := `command -v llvm-profdata || echo xcrun llvm-profdata`

setup:
    meson setup builddir

# debug build with AddressSanitizer and UBSan
build:
    @if [ ! -d "builddir" ]; then meson setup builddir; fi
    meson compile -C builddir

# -O3, link-time optimization, no sanitizers
release:
    @if [ ! -d "builddir-release" ]; then meson setup builddir-release --buildtype=release -Db_sanitize=none -Db_lto=true; fi
    meson compile -C builddir-release

# profile-guided release build: an instrumented build boots the DSR9500 firmware first
pgo:
    @if [ ! -d "builddir-pgo" ]; then meson setup builddir-pgo --buildtype=release -Db_sanitize=none -Db_lto=true; fi
    meson configure builddir-pgo -Db_pgo=generate
    meson compile -C builddir-pgo
    rm -f builddir-pgo/*.profraw
    LLVM_PROFILE_FILE=builddir-pgo/st20emu-%p.profraw ./builddir-pgo/st20emu {{pgo_training}} > builddir-pgo/training.log
    {{llvm_profdata}} merge -output=builddir-pgo/default.profdata builddir-pgo/*.profraw
    meson configure builddir-pgo -Db_pgo=use
    meson compile -C builddir-pgo

run: build
    ./builddir/st20emu

test: build
    meson test -C builddir --print-errorlogs

# differential CPU tests against the ST20 toolset simulator (needs ST20SIM_HOST and ST20SIM_ROOT)
test-st20sim *args: build
    node tests/st20sim_differential.js {{args}}

clean:
    @echo "Cleaning up build directories..."
    rm -rf builddir builddir-release builddir-pgo
