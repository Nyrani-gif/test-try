# Сборка Aegisub Sanae на Windows

Запускайте Meson из **x64 Native Tools Command Prompt for VS 2022** либо после
`Enter-VsDevShell -DevCmdArguments "-arch=x64 -host_arch=x64"`.

Git for Windows содержит нужный FFmpeg `gzip.exe`, но не добавляйте целиком
`C:\Program Files\Git\usr\bin` в начало `PATH`: находящийся там `link.exe`
конфликтует с линкером MSVC. Проект сначала ищет обычный `gzip`, затем явно
проверяет `C:/Program Files/Git/usr/bin/gzip.exe`. Готовый native-файл можно
подключить при первой настройке:

```powershell
meson setup build --native-file build-aux/windows-msvc.ini
meson compile -C build
```

Для уже настроенного каталога `build` native-файл обычно не нужен: достаточно
оставить Visual Studio environment активным и выполнить:

```powershell
meson compile -C build
```

Проверка перед конфигурацией:

```powershell
(Get-Command cl.exe).Source
(Get-Command link.exe).Source
Test-Path 'C:\Program Files\Git\usr\bin\gzip.exe'
```

Пути `cl.exe` и `link.exe` должны вести в Visual Studio, а не в Git.
