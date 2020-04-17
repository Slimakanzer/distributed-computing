# Лабораторные по распределенным вычислениям

В данном репозитории собраны все 6 работ по распределенным вычислениям. 

## Prerequisites

* Clang: `sudo apt-get update && sudo apt-get install clang`
* CMake: `sudo apt-get update && sudo apt-get install cmake` (минимум 3.5 версия)

## Build

Перейдите в дирректорию репозитория и выполните последующие комманды:

```sh
mkdir build
cd build
cmake ..
make
```

После билда executable файлы будут лежать в `path-to-repository/build/src/pa(тут номер pa)/`