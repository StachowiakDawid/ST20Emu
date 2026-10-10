default: build

setup:
    meson setup builddir

build:
    @if [ ! -d "builddir" ]; then meson setup builddir; fi
    meson compile -C builddir

run: build
    ./builddir/st20emu

test: build
    meson test -C builddir --print-errorlogs

clean:
    @echo "Cleaning up build directory..."
    rm -rf builddir
