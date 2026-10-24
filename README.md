# NFSMWCashForDamage

ASI-плагин для NFS: Most Wanted (2005, PC, `speed.exe` v1.3): в карьере рост «Ущерба государству» во время погони прибавляется к деньгам игрока.

## Установка

1. Нужен Ultimate ASI Loader (`dinput8.dll` рядом с `speed.exe`).
2. Скопируйте `NFSMWCashForDamage.asi` и `NFSMWCashForDamage.ini` в папку `scripts` игры.
3. Настройки в `.ini`: `Multiplier` (множитель), `Source` (`CostToState` или `PlayerDamage`), `Log`.

Чтобы удалить плагин, удалите эти файлы. Сборка: `build.bat` (нужны VS 2022 Build Tools, x86), результат появится в `dist`.
