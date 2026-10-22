# Этап 1. Исследование: Cost to State и деньги в NFS MW 2005 (PC v1.3)

Дата: 2026-10-23. Целевой exe: `speed.exe` 6 029 312 байт, MD5 `C0516B485065FABDD69579816B5DF763`,
PE timestamp `0x438E4C8C`, ImageBase `0x00400000`.

Все адреса ниже взяты из карты символов PC v1.3 из декомпиляции и/или прочитаны
дизассемблером (`dumpbin /disasm`) прямо из этого `speed.exe`. Адресов «по памяти» нет.

## Источники

| Репозиторий | Коммит | Что дал |
|---|---|---|
| [NFSPluginSDK](https://github.com/berkayylmao/NFSPluginSDK) | `d238bdf` (2024-12-22) | Структуры MW05: `AIPerpVehicle`, `IPerpetrator`, `CareerSettings`, `cFrontEndDatabase`, `GameFlowState` |
| [dbalatoni13/nfsmw](https://github.com/dbalatoni13/nfsmw) | `80993f3` (2026-10-10) | Исходники логики (`AIVehicle.cpp`, `AIPursuit.cpp`, `FeCostToState.cpp`) и **`config/SPEED_EXE_1_3/symbols.json`** — экспорт IDA именно для PC v1.3 с именами функций, vtable и глобальных переменных |
| [mw-takedown-n20](https://git.gay/eatincrispies/mw-takedown-n20) | `c4f53af` (2026-07-31) | Эталон той же сборки exe (тот же MD5), подход к проверке версии (сравнение байтов пролога), смещение `AIPursuit+0x13C` (сбитые копы) |

Декомпиляция — это в основном исходники консольных версий, но `symbols.json` относится к PC v1.3:
сегменты (`.text` 0x401000–0x890000, `.data` до 0x9C7000) совпадают с нашим exe.

## 1. Деньги в карьере

- `CareerSettings::CurrentCash` — `int32` по смещению `+0x0C` (SDK; подтверждено кодом
  `CareerSettings::GetCash` 0x50E300 `mov eax,[ecx+0Ch]`, `AwardCash` 0x566010, `SpendCash` 0x56D7A0).
- Путь к `CareerSettings` — одинаковый в четырёх местах кода игры (магазин тюнинга
  `CarCustomizeManager::Checkout` 0x7BAC19, выкуп машины `QRCarSelectBustedManager` 0x7AD568 и др.):

  ```
  CareerSettings* = [[0x0091CF90] + 0x10] + 0xA8
  CurrentCash     = [[0x0091CF90] + 0x10] + 0xB4      (int32)
  ```

  `0x91CF90` — `?FEDatabase@@3PAVcFrontendDatabase@@A` (карта символов; совпадает с SDK
  `cFrontEndDatabase::g_mThis`). `+0x10` — `CurrentUserProfiles[0]`, `+0xA8` — `mTheCareerSettings`.
- `SpendCash` не даёт уйти ниже нуля, `AwardCash` просто прибавляет без проверок.
  В плагине прибавлять с защитой от переполнения `int32`.

Уверенность: **высокая** (статический анализ, 4 независимых места в коде).

## 2. Cost to State: в игре это две разные величины

### 2.1. Счётчик нарушителя — `AIPerpVehicle::mCostToState`

- `int32` в объекте нарушителя. Смещение на PC: `[IPerpetrator + 0x20]`
  (`GetCostToState` 0x409050: `mov eax,[ecx+20h]; ret`).
- Увеличивается **только** в `AIPerpVehicle::AddCostToState` (0x409410). Условия
  (дизассемблировано, совпадает с декомпиляцией):
  1. `GRaceStatus::fObj` (0x91E000) не null;
  2. у машины есть погоня: `IVehicleAI::GetPursuit()` ≠ null;
  3. контекст заезда — карьера (`[GRaceStatus + 0x1964] == 2`, `GRace::kRaceContext_Career`)
     **или** тип заезда — испытание (`GetRaceType() == 8`, Challenge Series).
  
  Затем `mCostToState += cost` и `pursuit->NotifyPropertyDamaged(cost)`.
- Кто зовёт `AddCostToState`: `OnCausedCollision` (0x4299B0) и `OnCausedExplosion` (0x4163F0).
  Там тоже стоит проверка `ipursuit != nullptr`. Сумма берётся так:
  - удар по объекту (столб, остановка и т. п.) — атрибут `COST_TO_STATE` этого объекта;
  - удар по копу — 2000 напрямую или 500 косвенно, масштабируется скоростью, кратно 50, минимум 50;
  - взрыв копа — 2000.
- Обнуления в ходе игры не нашёл. Прямых вызовов `SetCostToState` (0x409040) нет. В функциях
  конца погони (`PursuitIsEvaded`, `CommitPursuitDetails`) вызовы слота `+0x20` возвращают
  значение, то есть это не `SetCostToState`. Остальные вызовы через vtable не проверял.
  Значение задаётся 0 в конструкторе (декомпиляция, `AIVehicle.cpp:1941`) и дальше, судя по
  всему, копится. Для плагина это не важно: падение значения обрабатывается как «новая база».

### 2.2. Итог погони — `AIPursuit::CalcTotalCostToState()` (0x40AA00)

Это **то число, которое игра показывает на экране как «Ущерб государству»**.
`LocalPlayer::UpdateHud` (0x6F13F0), участок 0x6F2075–0x6F20BD:
`ICostToState->SetInPursuit(true)`, при новой погоне `SetCostToState(0)`, затем
`SetCostToState(pursuit->CalcTotalCostToState())`. Без погони вызывается `SetInPursuit(false)`.
Виджет (`CostToState::SetCostToState` 0x566980) показывает окошко на 120 кадров, когда значение растёт.

Формула на PC (дизассемблировано; поля относительно `IPursuit*`, который возвращает `GetPursuit()`):

| Поле | Смещение | Вес |
|---|---|---|
| сбитые копы | `+0x13C` | ×5000 |
| вызванные вертолёты | `+0x150` | ×2000 |
| заслоны / сбитый трафик | `+0x158`, `+0x168` | ×500 |
| вызванные патрули / шипы / повреждённые копы | `+0x15C`, `+0x17C`, `+0x184` | ×250 |
| шипы с вертолёта | `+0x180` | ×225 |
| машины поддержки | `+0x188` | ×450 |
| урон имуществу от игрока | `+0x174` | ×1 |

Соответствие полей конкретным счётчикам взято из формулы декомпиляции (`AIPursuit.cpp:733`).
Для плагина важна только сумма, а её даёт сама функция.

`+0x174` (`mPropertyDamageValue`) — это сумма всех `AddCostToState` за текущую погоню:
`NotifyPropertyDamaged` (0x433A10) делает `[+0x174] += cost; [+0x178]++`. Вызывается только
из `AddCostToState` (других вызовов слота `+0xF4` на объектах погони нет).

Итог: **в итог погони входят и расходы полиции** (вызов вертолёта, заслоны, подкрепление).
То есть сумма растёт и тогда, когда игрок ничего не ломал.

## 3. Растёт ли Cost to State вне погони

**Нет.** Оба счётчика существуют только при активной погоне:
- `AddCostToState` выходит сразу, если `GetPursuit()` = null (проверено в коде PC);
- его вызывающие тоже проверяют `ipursuit != nullptr`;
- итог считается по полям объекта `AIPursuit`, который живёт только во время погони.

Вне погони столбы и остановки ломаются без всякого учёта стоимости. Вариант на будущее
(после MVP, вместе с хуками): перехватить `AIPerpVehicle::OnCausedCollision` (0x4299B0) и при
отсутствии погони брать стоимость объекта из того же атрибута `COST_TO_STATE`. Тогда
источник цен совпадёт с тем, что игра использует в погоне.

## 4. Как получить погоню и нарушителя игрока

У игры есть готовая функция `GManager::GetPlayerPursuitInterfaces(IPursuit*&, IPerpetrator*&)`
(0x5EB9E0). Она устроена так (дизассемблировано), и ту же цепочку можно повторить одним
чтением памяти, без вызова функций игры из фонового потока:

```
n       = [0x0092D884]                 ; размер списка локальных игроков (ListableSet<IPlayer>, bucket Local)
iplayer = [[0x0092D87C]]               ; первый элемент; vtable должен быть 0x008B0B10 (LocalPlayer как IPlayer)
simable = [iplayer - 0x08]             ; LocalPlayer::GetSimable: thunk 0x6F8FB0 (sub ecx,20h) -> 0x6F8F30 (mov eax,[ecx+18h])
comobj  = [simable + 0x04]             ; UTL::COM::Object
begin   = [comobj + 0x04], end = [comobj + 0x08]   ; массив пар {handle, интерфейс} по 8 байт, отсортирован по handle
                                       ; (_IList::Find 0x5D59F0 + lower_bound 0x5D49F0)
ivehai  = пара с handle 0x00403700     ; IVehicleAI::_IHandle; vtable должен быть 0x008929F0 (AIVehicleHuman)
iperp   = пара с handle 0x004037E0     ; IPerpetrator::_IHandle; vtable должен быть 0x00892988 (AIVehicleHuman)
pursuit = [ivehai + 0x70]              ; AIVehicle::GetPursuit 0x431D70; null вне погони; vtable 0x00892770 (AIPursuit)
```

Связь проверена ещё и по `AddCostToState`: там `IVehicleAI = IPerpetrator − 0x70C`, и через него
вызывается `GetPursuit` (слот `+0xA0`). В плагине это используется как дополнительная проверка
`iperp − ivehai == 0x70C`.

## 5. Состояние игры

- `[0x00925E90]` — `GameFlowState` (`?TheGameFlowManager@@3...` в карте символов, SDK то же;
  в коде 40 сравнений с 6 и 27 — с 3). Значения из декомпиляции (`GameFlow.hpp`):
  3 = в меню, 6 = в заезде/свободной езде (`RACING`), 4/5/7/8 = загрузки и выгрузки.
- Карьера: `[[0x0091E000] + 0x1964] == 2` (`GRace::kRaceContext_Career`; 0 = быстрая гонка,
  1 = онлайн). Это ровно то условие, которое игра сама проверяет в `AddCostToState`.
  Испытания (Challenge Series) так отсекаются: там Cost to State тоже копится, но денег карьеры нет.

## 6. Что не взял из SDK

`PVehicle::g_mInstances = 0x9352B0` из SDK в коде этого exe не встречается ни разу.
Не используем: путь через `LocalPlayer` подтверждён кодом игры.

## 7. Сигнатуры для проверки версии в плагине

Перед работой плагин сравнивает байты в памяти по адресам, на которые опирается:

| Адрес | Байты | Что |
|---|---|---|
| 0x409410 | `A1 00 E0 91 00 85 C0 57 8B F9` | `AddCostToState` (ссылка на `GRaceStatus::fObj`) |
| 0x409050 | `8B 41 20 C3` | `GetCostToState` (смещение 0x20) |
| 0x40AA00 | `8B 81 3C 01 00 00 8B 91 50 01 00 00` | `CalcTotalCostToState` |
| 0x431D70 | `8B 41 70 C3` | `AIVehicle::GetPursuit` (смещение 0x70) |
| 0x5EB9E0 | `A1 84 D8 92 00 85 C0 56 74 14 A1 7C D8 92 00` | список локальных игроков |
| 0x6F8FB0 | `83 E9 20 E9 78 FF FF FF` | thunk `GetSimable` (−0x20) |
| 0x6F8F30 | `8B 41 18 C3` | `GetSimable` (+0x18) |
| 0x7BAC19 | `A1 90 CF 91 00 8B 48 10 56 81 C1 A8 00 00 00` | путь к `CareerSettings` |
| 0x56D7A0 | `8B 41 0C 8B 54 24 04` | `SpendCash` (смещение денег 0x0C) |

Плюс PE timestamp `0x438E4C8C` в заголовке загруженного модуля.

## 8. Уверенность и что подтвердить в игре

Всё выше получено статически из кода exe, поэтому уверенность **высокая**. Что можно проверить
только на запущенной игре:

1. Цепочка из п. 4 доходит до объектов (в лог плагин пишет найденные указатели и vtable).
2. Число в логе = число «Ущерб государству» на экране во время погони.
3. Число денег в логе = деньги в меню карьеры.

Это делается диагностическим логом плагина. Cheat Engine нужен только как запасной вариант.
