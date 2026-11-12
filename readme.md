<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 Marco Pistella -->

# RPNCalc

A Reverse Polish Notation (RPN) calculator, as a plug-in for **Far Manager 3** (32 and 64 bit).

Author: Marco Pistella · Version 1.0.1 · License: [MIT](LICENSE)

RPNCalc is used in two ways:

- a **dialog**, opened from the plug-ins menu (`F11`, "RPN Calculator"), that shows the stack and can be worked with the keyboard alone;
- the **Far command line**, with the prefix `rpn:`, for a quick calculation:

  ```
  rpn: 3 4 + 5 *
  ```

  The result is shown in a message box.

The stack and the angle mode are shared by the dialog and the command line and are kept between sessions.

## What RPN means

Operands come first and the operator after them. Values are kept on a stack: X is the top, Y the level below it, and so on. A number is pushed on the stack; an operator takes its operands from the stack and pushes the result.

```
3 4 + 5 *          ->  35

3       stack: 3
4       stack: 3 4
+       stack: 7         (3 + 4)
5       stack: 7 5
*       stack: 35
```

There are no parentheses: the order of the commands is the order of evaluation. For an operator that is not commutative, Y is the left operand and X the right one: `10 4 -` gives 6 and `2 3 ^` gives 8.

## Features

- Stack of up to 10000 entries; the dialog shows the first 8 levels.
- Binary operators, functions, constants and stack commands (see [Commands](#commands)).
- Radians or degrees for the trigonometric functions.
- Integers in binary, hexadecimal and decimal (`1010#B`, `FF#H`, `255#D`) and the `bin`, `hex`, `dec` display commands.
- Undo: 100 steps, shared by the dialog and the command line.
- Every line is atomic: if it contains an error (or if memory runs out), the stack, the angle mode and the undo history return to the state they had before the line.
- The stack and the angle mode are saved in the Far settings of the plug-in.
- All the texts come from a Far language file (`.lng`), so the plug-in can be translated without recompiling. The English texts are also built into the DLL.
- Numbers are read and written with the C runtime, so the result does not depend on the Windows locale.

## Requirements and installation

- Far Manager 3.0 (build 4000 or later), 32 or 64 bit. Use the DLL of the same bitness as Far (`X86\RPNCalc.dll` or `x64\RPNCalc.dll`).
- Put `RPNCalc.dll`, the help file `RPNCalcEng.hlf` and the language file `RPNCalcEng.lng` together in a folder of their own under the Far plug-ins folder, for example:

  ```
  %FARHOME%\Plugins\RPNCalc\
  ```

  and restart Far. Far looks for the help and language files in the folder of the DLL, so the folder must not be shared with another plug-in.
- To calculate, press `F11` and choose "RPN Calculator", or type `rpn:` followed by the commands in the Far command line. The plug-in never opens a panel.

## The dialog

Type one or more commands in the input line, separated by white space, and press `Enter`. The results go to the stack and the line is cleared. If the line contains an error, the error is shown in the status line and the text stays in the input line so that it can be corrected.

Keys:

- `Enter`: evaluate the line. On an empty line: DUP.
- `+` `*` `/` `^`: on an empty line, apply the operator at once.
- `Backspace`: on an empty line, DROP (remove X).
- `Del`: on an empty line, CLR (clear the whole stack).
- `Ctrl+Backspace`: UNDO. It works also when the line contains text; the text is left untouched.
- `Alt+F1`: `-` (Y - X)
- `Alt+F2`: `chs` (change sign)
- `Alt+F3`: `inv` (1/X)
- `Alt+F4`: `swap`
- `Ctrl+Enter`: evaluate, then close the dialog and insert X into the Far command line, with all the digits it needs. On an empty line it does not DUP: it only inserts X. If the stack is empty (for example after `clr`), the status line says so and the dialog stays open; the line has been evaluated and is cleared, as with `Enter`. If Far does not insert the text, a message box shows X, so that it is not lost.
- `F1`: help.
- `Esc`: close the dialog. The stack is saved.

"Empty line" means a line with no characters at all. A line that holds only spaces is not empty: `Backspace`, `Del` and the operator keys then edit the line as usual.

The text already typed is evaluated before an `Alt+F1` ... `Alt+F4` command, as part of the same line. If the terminal intercepts `Alt+F4`, type `swap`.

## The command line

Type `rpn:` followed by the commands in the Far command line and press `Enter`:

```
rpn: 2 pi *
```

- A message box shows X and the stack depth ("Stack is empty" if nothing is left).
- If the line contains an error, the box shows the error and the stack is left as it was.
- If the stack and the angle mode cannot be saved in the Far settings, the last line of the box says so (it is a warning); the result is shown all the same.
- `F1` in the message box opens the help.
- To copy X from the dialog to the command line, press `Ctrl+Enter` in the dialog.

## Commands

Commands are separated by white space; several can be on one line. Their names are not case sensitive.

**Numbers**

```
3.14   3,14   -2   1e-5
```

Both `.` and `,` are accepted as the decimal separator. Binary `1010#B`, hexadecimal `FF#H`, decimal integer `255#D`: the sign is optional, the digits are 0-9 and A-F, and the absolute value must not exceed 2^53. `inf`, `nan` and hexadecimal floating-point numbers (such as `0x1p3`) are not accepted. A number too large for a double is not accepted, and neither is one that is not zero but too small to be represented (`1e-400`: it would be read as 0); subnormal values, `0` and `-0` are accepted.

**Binary operators** (Y op X; the result replaces both operands)

```
+  -  *  /  ^  **  %  mod  atan2  min  max
```

`^` and `**` are the power Y^X. `%` and `mod` are the remainder, with the sign of the dividend. `atan2` computes atan2(Y, X) in the current angle mode. Division by zero (`/`, `%`, `mod`) is an error. For equal values, `min` and `max` take -0 as smaller than +0, whatever the order of the operands.

**Functions** (act on X)

- `neg` `chs`: change sign
- `abs`: absolute value
- `inv`: 1/X (X must not be 0)
- `sq`: X squared
- `sqrt`: square root (X must not be negative)
- `cbrt`: cube root
- `exp`: e raised to the power X
- `ln`, `log`, `log2`: natural, base 10 and base 2 logarithm (X must be greater than 0)
- `sin` `cos` `tan`: X is in the current angle mode
- `asin` `acos`: X must be in -1..1; the result is in the current angle mode
- `atan`: arc tangent, in the current angle mode
- `sinh` `cosh` `tanh`: hyperbolic functions (not affected by the angle mode)
- `fact`: factorial of an integer X in 0..170 (exact up to 22!, the nearest double beyond)
- `floor` `ceil` `round` `int`: floor, ceiling, nearest integer (halves away from zero), truncation toward zero

For `sin`, `cos` and `tan`, a result smaller than four units in the last place of the argument (about as much as the argument is known to; for example `pi sin`, or `180 sin` in degrees) is replaced by exactly 0, even if the function really has a tiny value there: `3.1415926535897936 sin` is -3.2e-16 and is shown as 0. This is not done when |X| is 1e9 or more. Small results of small arguments (`1e-16 sin`) are kept. `tan` gives no error at its poles: `90 deg tan` gives a very large number (about 1.6e16). A result that is not finite is an error.

**Stack commands**

- `dup`, `drop`, `swap`, `over`
- `rot`: rotate the top three levels (Z -> X, X -> Y, Y -> Z)
- `clr` `clear`: clear the stack
- `sum`, `avg`: replace the whole stack with the sum or the average of its entries

**Constants:** `pi`, `e`

**Angles:** `rad` (default), `deg`

**Undo:** `undo` restores the stack and the angle mode to the state before the last command that changed them. Every command that changes something is one step, numbers included: `3 4 +` takes three undos. There is no redo.

**Number bases:** `bin`, `hex`, `dec` change the display of X, which must be an integer (|X| <= 2^53):

```
255 bin      ->  11111111#B
255 hex      ->  FF#H
FF#H dec     ->  255
```

The value itself does not change: it is only shown in another base. Negative numbers are shown with a minus sign, not in two's complement.

## Errors and persistence

A line that contains an error is cancelled as a whole. The error is shown in the status line of the dialog, or in the message box of `rpn:`.

The stack and the angle mode are stored in the Far settings of the plug-in when the dialog is closed and after every successful `rpn:` line, and are read the first time the plug-in is used in a Far session. The stack is stored as text, in the same syntax that the calculator accepts, with 17 significant digits so that every value is read back exactly. The undo history is not saved. The results of the calls to Far are checked: if the stack and the angle mode cannot be saved, the message "Cannot save the stack and the angle mode in the Far settings" is shown and the calculator goes on working. If Far cannot open the settings when the plug-in is first used, the calculator works on an empty stack and the stored one is not overwritten: nothing is saved until it has been read, and the next use tries again.

## Limits

- Stack depth: 10000 entries. The dialog shows the first 8 levels.
- Undo: 100 steps.
- Number bases: X must be an integer with |X| <= 2^53.
- Numbers are IEEE double precision; results are shown with up to 15 significant digits.
- A decimal number written with 512 characters or more is rejected, and so is one that is not zero but smaller than about 4.9e-324 in absolute value.
- If memory runs out, the plug-in reports it and Far goes on; the line being evaluated is cancelled and the stack is as it was before it.

## Languages

Every text that the plug-in shows comes from a Far language file in the folder of the DLL. `RPNCalcEng.lng` is the English one; the help is the separate file `RPNCalcEng.hlf`. To add a language, no recompilation is needed:

1. Copy `RPNCalcEng.lng` to `RPNCalc<Language>.lng` in the folder of the DLL.
2. On the first line, change the language to the name that Far uses for it (the word before the comma, for example `German`).
3. Translate the text of each message, between the double quotes.
4. If the translation has characters outside ASCII, save the file as UTF-8 with BOM (or UTF-16). Restart Far.

Keep every message, in the same order, and keep `%s` once in each message that has it. The names of the commands and the prefix `rpn:` are not translated.

A new version of the plug-in that needs more messages adds them at the end of the file: a language file made for an earlier version keeps working, but Far reports a bad language file (and the English text is used) when one of the new messages is needed, so copy them from `RPNCalcEng.lng`. Version 1.0.1 adds the last two (the file has 29 messages).

## Building from source

The plug-in is a single C++ file, `rpncalc.cpp`, plus the version resource `rpncalc.rc`. You need [MSYS2](https://www.msys2.org) (found automatically, usually in `C:\msys64`; otherwise set `MSYS2_ROOT`; if it is not found, `build.bat` says so and prints this address). From the Windows command prompt, in the folder of the project:

```
build.bat          builds both versions, compressed with UPX when it is found
build.bat x64      builds .\x64\RPNCalc.dll (64 bit)
build.bat x86      builds .\X86\RPNCalc.dll (32 bit)
build.bat noupx    does not compress the DLLs (can be combined with x64 or x86)
build.bat zip      also creates RPNCalc-<version>.zip, for example RPNCalc-1.0.1.zip, with the folders built, RPNCalc.txt, CHANGELOG.TXT and LICENSE
```

`RPNCalcEng.lng` and `RPNCalcEng.hlf` are copied next to each DLL, so that the folder is ready to be copied into the Far plug-ins folder.

- **Compilers.** The 64 bit DLL is built with the g++ of the MSYS2 environment `mingw64` or, if it is missing, `ucrt64`; the 32 bit DLL with the one of `mingw32` (the environments tried can be set with `ENV64` and `ENV32`). If the compiler of a DLL is missing, `build.bat` lists the compilers it can install with `pacman` (`mingw-w64-x86_64-gcc` and `mingw-w64-ucrt-x86_64-gcc` for the 64 bit DLL, `mingw-w64-i686-gcc` for the 32 bit one) and asks which one to install, or none. With none, with an answer that is not valid, or if `pacman` fails, the build of that DLL stops with an error. Nothing is installed without that answer. If MSYS2 itself is not found, `build.bat` only says where to download it. A DLL built with `mingw64` needs only `msvcrt.dll`; one built with `ucrt64` needs the UCRT (Windows 10, or 7/8.1 with the update). MSYS2 has deprecated `mingw64` and is phasing out `mingw32` (see <https://www.msys2.org/docs/environments/>), so those packages may disappear.
- **plugin.hpp.** The Far 3 header is taken from the folder of the project, from the folder `FARSDK` (default `.\PluginSDK`) or from the `PluginSDK\Headers.c` folder of an installed Far. If it is not found, it is downloaded once with `curl` from the official Far Manager repository, into the folder of the project.
- **UPX.** Used when `upx.exe` is found (the MSYS2 folders or the `PATH`). Some antivirus programs are suspicious of UPX-packed files: use `noupx` for a DLL that is not packed.
- **Version.** `build.bat` stops if the version in `rpncalc.rc` differs from the `PLUGIN_VER_*` values at the top of `rpncalc.cpp`.
- **Exports.** Both DLLs are linked with `-Wl,--kill-at`. After linking, `build.bat` checks with `objdump` that `GetGlobalInfoW`, `SetStartupInfoW`, `GetPluginInfoW` and `OpenW` are exported with undecorated names.

Compile options (`-D` on the compiler command line): `RPN_MIN_FAR_BUILD` (the oldest Far build that loads the plug-in, default 4000) and `RPN_KEEP_FUNCTEXCEPT` (keep the exception helpers of libstdc++ instead of the small ones in the source).

## Files

- `rpncalc.cpp`: the plug-in
- `rpncalc.rc`: the version resource of the DLL
- `build.bat`: the build script
- `RPNCalcEng.lng`: the language file (English)
- `RPNCalcEng.hlf`: the help file (English)
- `RPNCalc.txt`: the full documentation
- `CHANGELOG.TXT`: the changes of each version
- `LICENSE`: the MIT License

## License

RPNCalc is released under the [MIT License](LICENSE). Copyright (c) 2026 Marco Pistella.

The copyright notice and the permission notice in [LICENSE](LICENSE) must be included in all copies or substantial portions of the software; the package created by `build.bat zip` contains the `LICENSE` file. The software is provided "as is", without warranty of any kind.

### Third-party files

The `PluginSDK` folder contains files of the [Far Manager](https://github.com/FarGroup/FarManager) SDK, Copyright © 1996 Eugene Roshal and © 2000 Far Group (`DlgBuilder.hpp`: © 2009 Far Group). They are not part of RPNCalc and are not covered by the MIT License: each file keeps its own license header (3-clause BSD), which must be left in place. `plugin.hpp` is the only file that the build needs; its header also states that Far Manager plug-ins that use it can be distributed under any other license.
