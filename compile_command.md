# Compiling UZDoom

To compile **UZDoom** inside your `bazzite-dev` distrobox container:

## Incremental Build / Recompile
If you have already configured the build directory, compile using:
```bash
distrobox enter bazzite-dev -- make -C build -j$(nproc)
```

## Fresh Build / Configuration
If you need to configure the build directory from scratch:
```bash
distrobox enter bazzite-dev -- cmake -B build -DCMAKE_BUILD_TYPE=Release
distrobox enter bazzite-dev -- cmake --build build -j$(nproc)
```
