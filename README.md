# Car Display for Cedarville's Supermileage 
### Orginal Design by Zaine F, and the Supermileage Computer Team of 2025-2026

![Car Display Demo](https://github.com/HEEV/CarDisplay/blob/main/demo.png?raw=true)

Host Simulation program to preview and prepare a system natively, without the use of a RasPi 5. 
Based on LVGL Linux Port, reqiures GCC and SDL for Host rendering.

Prerequisites:

``` shell
# Debian / Ubuntu
sudo apt install build-essential cmake libsdl2-dev

# macOS (Apple Silicon and Intel both work; builds with AppleClang)
brew install cmake sdl2
```

Compilation command:
``` shell 
mkdir build
cd build
cmake ..
cd ..
cmake --build build
```

Run with:

``` shell 
./bin/main
```