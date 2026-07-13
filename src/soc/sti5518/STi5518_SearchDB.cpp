#include "STi5518_SearchDB.h"

#include "STi5518_Region0.h"
#include "STi5518_RegisterDB.h"

#include "../../common/compat.h"

#include <algorithm>
#include <iterator>

namespace soc::sti5518 {

bool search_for_reg(uint32_t reg_addr) {
  if (reg_addr >= 0xc0000000 && reg_addr <= 0xc0800000) { // Shared SDRAM
    compat::println("DESC: Address is into Shared SDRAM (Region 1)");
  } else if (reg_addr >= 0x80001000 && reg_addr <= 0x80001800) { // 2 Kb SRAM
    compat::println(
        "DESC: Internal SRAM if data cache is not enabled. User-code, data and stack (Region 0)");
  } else if (reg_addr >= 0x80000000 && reg_addr <= 0x80000fff) { // 4 Kb SRAM
    auto it = std::ranges::find_if(region0_entries, [reg_addr](const auto &entry) {
      return reg_addr >= entry.lbaddr && reg_addr <= entry.hbaddr;
    });

    if (it != region0_entries.end()) {
      compat::println("DESC: {}", it->description);
    } else {
      compat::println("DESC: Address is into 4Kbyte SRAM (Region 0)");
    }
  } else if (reg_addr >= 0x70000000 && reg_addr <= 0x7fffffff) { // EMI Bank 3
    compat::println("DESC: Address is into EMI BANK 3 (Region 3)");
  } else if (reg_addr >= 0x60000000 && reg_addr <= 0x6fffffff) { // EMI Bank 2
    compat::println("DESC: Address is into EMI BANK 2 (Region 3)");
  } else if (reg_addr >= 0x50000000 && reg_addr <= 0x5fffffff) { // EMI Bank 1
    compat::println("DESC: Address is into EMI BANK 1 (Region 3)");
  } else if (reg_addr >= 0x40000000 && reg_addr <= 0x4fffffff) { // EMI Bank 0
    compat::println("DESC: Address is into EMI BANK 0 (Region 3)");
  } else if (reg_addr >= 0x20040000 && reg_addr <= 0x3fffffff) { // Reserved
    compat::println("DESC: Address is into Reserved Memory Area (Region 2)");
  }

  if (reg_addr <= 0x2003ffff) { // Peripheral configuration registers (Region 2)
    auto it = std::ranges::lower_bound(db_regs_entries, reg_addr, {}, &RegisterDescription::addr);

    if (it != std::end(db_regs_entries) && it->addr == reg_addr) {
      compat::println("REGN: {} bits:{} access:{}\nDESC: {}", it->regname, it->bits, it->access,
                      it->description);
      return true;
    } else {
      compat::println("Sorry, no match for 0x{:08x} in Registers DB", reg_addr);
      return false;
    }
  }

  return false;
}

} // namespace soc::sti5518
