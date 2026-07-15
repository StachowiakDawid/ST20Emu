# ST20Emu

ST20Emu is an emulator for the ST20, a 32-bit transputer-based processor architecture developed by SGS-Thomson (later STMicroelectronics). Widely used throughout the late 1990s and early 2000s, ST20 cores powered many iconic digital Set-Top Boxes (STBs) and DVD players of the era.

## Development

### Requirements

- clang 18.1.3
- make

### Building

Build a debug version:

```bash
make clean && make
```

Build a release version (it's too broken for now anyway and will silence runtime errors):

```bash
BUILD=release make clean && make
```

## Getting started

Run the binary from your terminal:

```bash
./st20emu
```

Once inside the interactive command prompt (`>`), load a binary file containing valid ST20 instructions (such as a TSOP dump) using the `l` command:

```plaintext
l firmware/6300.bin
```

> **Note:** You can load any compatible ST20 binary. Ensure you load a file before starting execution; attempting to run the emulator with empty memory will result in immediate execution errors.

The emulator reads configuration values from `st20emu.ini`, which must reside in the same directory as the executable. The settings in this file are straightforward and self-explanatory.

> **Note:** The INI file is a legacy behaviour of old code. It is expected to be removed and replaced later.

Once your firmware is loaded, you can begin issuing commands to the emulator.

---

### Parameter definitions

Before using the commands, keep in mind these parameter meanings:

- `value` / `address`: A 32-bit **octal** value or address.
- `filename` / `name`: A target file or directory name.
- `register`: The specific register identifier (`a`, `b`, `c`, or `i`).
- `index`: An octal number representing a valid workspace pointer (`Wptr`) index.

---

### Emulator commands reference

| Category               | Command | Syntax               | Description                                                                                                                                                                                                                                                                                                                     |
| ---------------------- | ------- | -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **Execution**          | `ENTER` | _(Press Enter)_      | Executes the single next instruction.                                                                                                                                                                                                                                                                                           |
|                        | `g`     | `g`                  | Runs the emulation from the current address until a watch condition is met or 1,000,000 instructions pass. **Press `g` again during execution to stop.**                                                                                                                                                                        |
|                        | `s`     | `s register value`   | Sets a watch condition on `a`, `b`, `c`, or `i`. When the register hits this value, execution halts and prompts you.                                                                                                                                                                                                            |
| **Register control**   | `a`     | `a value`            | Sets the **A register** to the specified value.                                                                                                                                                                                                                                                                                 |
|                        | `b`     | `b value`            | Sets the **B register** to the specified value.                                                                                                                                                                                                                                                                                 |
|                        | `c`     | `c value`            | Sets the **C register** to the specified value.                                                                                                                                                                                                                                                                                 |
|                        | `i`     | `i value`            | Sets the **Iptr (Instruction pointer)** register to the specified value.                                                                                                                                                                                                                                                        |
|                        | `w`     | `w index value`      | Sets the workspace word at the specified `index` to the given value.                                                                                                                                                                                                                                                            |
| **Memory control**     | `l`     | `l address filename` | Loads a binary/TSOP dump file into memory starting at the specified address.                                                                                                                                                                                                                                                    |
|                        | `v`     | `v address`          | Views the word at the specified memory address.                                                                                                                                                                                                                                                                                 |
|                        | `va`    | `va address value`   | Writes/sets the word at the specified address with a new value.                                                                                                                                                                                                                                                                 |
|                        | `vaa`   | `vaa address range`  | Views a range of words starting from the specified address.                                                                                                                                                                                                                                                                     |
| **State & session**    | `load`  | `load name`          | Loads a previously saved ST20 state.                                                                                                                                                                                                                                                                                            |
|                        | `save`  | `save name`          | Dumps memory contents (in 8K chunks) and the CPU state into a new directory named `name`. The `.bin` files are the memory contents. These can be loaded into a hex editors or IDA Pro. The `.use` files are flags indicating if a particular byte in memory has been defined or not. The `cpu.bin` file contains the CPU state. |
|                        | `q`     | `q`                  | Quits the emulator.                                                                                                                                                                                                                                                                                                             |
| **Diagnostics & info** | `db`    | `db value`           | Searches the internal database for information about a specified register (passed as a hex number).                                                                                                                                                                                                                             |
|                        | `omr`   | `omr`                | Displays "Other Machine Registers" (clock, status registers, traps, interrupts).                                                                                                                                                                                                                                                |
|                        | `ver`   | `ver`                | Shows the contents of the "Enables" register with descriptive labels for every bit set to 1.                                                                                                                                                                                                                                    |
|                        | `vra`   | `vra`                | Toggles **Verbose Register Access** mode ON/OFF for verbose output on `devlb`/`sb` and `devlw`/`sb` instructions. Only STi5518 regs are supported at moment.                                                                                                                                                                    |

---

### Execution output

After a command is executed or `ENTER` is pressed, the emulator prints standard status block:

- Displays the current hex values for `A`, `B`, `C` and the `Iptr` registers
- Displays all currently allocated workspace words.

  > Note: Unused words default to the bit pattern 0xAAAAAAAA

- Displays the memory address of the upcoming command, its raw octal byte values, and its decoded assembly mnemonic.
- Finally, it drops the cursor back to the command prompt (`>`), waiting for the input.

Here's an example of what you might see:

```plaintext
A=0x7fff0014 B=0xaaaaaaaa C=0xaaaaaaaa  Iptr=0x7fff0038
Wptr  0=0x7fff0014  1=0x7ffffff0  2=0xaaaaaaaa 3=0xaaaaaaaa
      4=0x7ffffff0

7fff0038  60 bd  ajw fffffffd
>
```

If you have `vra` mode turned on, you may see these additional diagnostic tags:

- `NOTE`: Standard informational note generated by `st20emu`.
- `REGN`: **Reg**ister **N**ame found in the internal hardware database.
- `bits:`: The bit-width extension of the register.
- `access:`: Allowed access permissions for the register (e.g., `R/W` for Read/Write).
- `DESC`/`DESCR`: Text **desc**ription of the register or the targeted memory region.

And this with the `vra` mode ON (if an entry is found in the internal DB):

```plaintext
7fff01a4  2f f1  devsb
>
NOTE: At 0x7fff01a4 Write to device at address 00000e00, value=0x00000007
REGN: MPEG_CONTROL_7_0 bits:8 access:R/W
DESC: MPEG Audio/Video buffer control register
A=0x7fff01d2 B=0xaaaaaaaa C=0xaaaaaaaa  Iptr=0x7fff01a6
Wptr  0=0x7fff01d2  1=0xaaaaaaaa  2=0xaaaaaaaa  3=0xaaaaaaaa
      4=0x7ffffff0  5=0xaaaaaaaa  6=0xaaaaaaaa

7fff01a6  27 40  ldc 70
>
```

This is the output of the `omr` command...

```plaintext

> omr
OTHER MACHINE REGISTERS

Enables=0xffffc000
ClockRegHP=0x20000000 ClockRegLP=0x20000000 ClockEnables=0x03
HP_ErrFlag=0x00 LP_ErrFlag=0x00 HaltOnError=0x00
>

```

...and this for `ver`

```plaintext
> ver
Enables Register Value=0xffffc000
 LP_PROCESS_INT_ENB     is set
 LP_TIMESLICE_ENB       is set
 LP_EXTERNALEVENT_ENB   is set
 LP_TIMER_ALRM_ENB      is set
 HP_PROCESS_INT_ENB     is set
 HP_TIMESLICE_ENB       is set
 HP_EXTERNALEVENT_ENB   is set
 HP_TIMER_ALRM_ENB      is set
>
```

---

`devls`/`lb`/`lw` and `devss`/`sb`/`sw` can be used for memory access too, so if a specific address isn't found in the internal DB, a more generic description is returned based on the datasheet memory map description.

Example:

```plaintext
NOTE: At 0xc0402804 Write to device at address c042a5e8, value=0x00000002
DESCR: Address is into Shared SDRAM (Region 1)
```

---

The ajw command has not been executed yet. If you hit the ENTER
key, it will be executed and the results of its execution will be
displayed.

## Acknowledgments

This project is a fork of the original [ST20Emu](https://github.com/IU5HKU/ST20Emu) created by Marco Campinoti.

## License

This project is licensed under the terms of the [GPL-3.0 license](https://github.com/StachowiakDawid/ST20Emu/blob/main/LICENSE).
