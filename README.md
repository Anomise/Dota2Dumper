# Dota 2 Schema Dumper

Дампер оффсетов Dota 2 через Source 2 Schema System.

## Скачать

Перейди во вкладку **Actions** → последний успешный билд → **Artifacts** → скачай `dota2-dumper`.

## Использование

1. Запусти **Dota 2**, зайди в лобби или матч
2. Положи `dumper.dll` рядом с `injector.exe`
3. Запусти `injector.exe` **от имени администратора**
4. Дождись завершения дампа в консоли Dota 2
5. Результат: `C:\dota2_dump\offsets.hpp` и `offsets.json`

## Сборка вручную

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
