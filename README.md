# ST20Emu

ST20 Emulator, an ARM-based MCU used in some 90's Set Top Box, this is an old project and now i think it's completely useless, but i've decided to put here the code that maybe can be useful to someone who wanna write a simple emulator.
The code can be useful in writing virtual machines too. Feel free to grab, use and misuse anything you want from this source code. The Project was compiled under Win (Visual Studio Professional 2013) and Linux, and run without efforts.
In the repo there is also a file called `6300.bin`, the firmware of a very very very old STB i bought so many years ago that i don't exactly remember when. This emu came back directly from the dust of my archive, as you can see i wrote the last version in january 2012, after that the project was abandoned due to lack of time to dedicate to it.
The instructions set manual is present too.

ST20 emulator V3.0

V2.0 March 15, 2002
V3.0 December 20, 2011
V3.1 December 31, 2011
V3.2 January 04, 2012

## Building

It was tested with clang version 18.1.3 (1ubuntu1).

```bash
make clean && make
```

## How to use

To start, type `st20emu` in a terminal window. At the `>`
prompt, type

```plaintext
l firmware/6300.bin
```

where `firmware/6300.bin` is the name of a file with ST20 instructions in
it (e.g. a TSOP dump). It can be replaced with different file. If you forget to do this, you'll get lots
of errors since the emulator won't have any ST20 instructions to
emulate.
Some settings are read from `st20emu.ini` file, that must be in the
same directory of the emulator. The content of this file is easily
understandable and the commands are self-explanatory.

Now you can start issuing commands to the emulator

Here is a reorganized, clean, and highly scannable reference table for the **ST20Emu Emulator Commands**, grouped by their primary function to make it easier to read than the original document.

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

devls/lb/lw and devss/sb/sw can be used for memory access too, so if a specific
address isn't found in the internal DB, a more generic description is returned
based over the Datasheet memory map description. Example:

NOTE: At 0xc0402804 Write to device at address c042a5e8, value=0x00000002
DESCR: Address is into Shared SDRAM (Region 1)

---

The ajw command has not been executed yet. If you hit the ENTER
key, it will be executed and the results of its execution will be
displayed.

## Notes

Unused words are given the bit pattern 0xAAAAAAAA

The workspace has been assigned to the memory addresses from
0x1FFFF000 to 0x1FFFFFFF. The first workspace word is stored
at 0x1FFFFFFC. The workspace words are stored at consecutively
lower words.

Only the most common ST20 instructions have been implemented.
The emulator will warn you when an unimplemented instruction
has been encountered.

I still have lots of things to add to this emulator

- implement the complete set of instructions
- allow people to give names to addresses
- add a command to step over subroutines
- etc.
  I'm willing to accept other suggestions but there's no guarantee
  that I'll get any of the work done very quickly.

## Troubleshooting

\*Mar 2002
This is a WIN32 app. I don't think it will run in a 16 bit
environment (does anyone actually use these any more?). I wrote
this for Win98. I have no idea whether it works in NT/2000/XP.

\*Dec 2011
Can be compiled with VC++ Express, work fine in 2K/XP/Win7.
