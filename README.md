# NFSMWCashForDamage

Need for Speed: Most Wanted (2005) ASI mod for PC with `speed.exe` v1.3. When Cost to State grows during a pursuit in Career mode, the same amount is added to your cash. You can now pay for upgrades by smashing things in front of the cops.

## Install

1. Install [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (`dinput8.dll` next to `speed.exe`).
2. Download `NFSMWCashForDamage.asi` and `NFSMWCashForDamage.ini` from [Releases](../../releases) and copy them to the game's `scripts` folder.
3. Change settings in the `.ini` if you like: `Multiplier` (cash per $1 of damage), `Source` (`CostToState` or `PlayerDamage`), `Log`.

To uninstall, delete both files. To build from source, run `build.bat` (needs Visual Studio 2022 Build Tools, x86). The output goes to `dist`.
