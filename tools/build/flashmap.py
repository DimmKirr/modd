# SPDX-License-Identifier: GPL-3.0-only
"""The SPI-flash layout as the package builder and the linker step see it.

The free numbers are Kconfig symbols (kconf/flash.kconfig); firmware/hal/fm1_flash_map.h
derives the same names in C for the app, the boot stub and the update loader. Keep the
formulas of the two in step (tests/build/flashmap_test.py pins both).
"""
import re
from dataclasses import dataclass

APP_LO = 0x4000                 # the head (SPL, ISD, directory) is below; the boot stub starts here
BODY_OFF = 0x5000               # the body (the app proper) follows the boot stub
MARK_TAIL = 0x2000              # the marker's sector is the last but one of the app area
MARK_SEC_OFF = 0xFE0            # the marker: the last 32 bytes of its sector
APP_SLOT_OVERHEAD = 0x1044      # area head, cfg_tool and EQ table around app.bin in the app area (V15's)
HW_APP_END = 0x93000            # the FM-1's map; every store above the app area follows this offset
STORE_A_LEN = 0x4000
DATA_LEN = 0x49000              # Melodee's main store
OTA_LEN = 0x5000                # staged loader
BANK_EXT_LEN = 0x16000
GLOB_GAP = 0x1000
GLOB_LEN = 0x3000               # Melodee globals; the key_mac sector follows
VM_LEN = 0x56000                # the "VM" region descriptor: app area end up to the BTIF sector
SMP_USER_OFF = 0x9000           # first user-sample slot, from the main store's start
SMP_USER_SIZE = 0x14000
SMP_USER_N = 3
SECTOR = 0x1000
FLASH_HDR_SIZE_OFF = 6          # the flash header's flash_size field (u32): the chip less the key_mac sector


@dataclass(frozen=True)
class Layout:
    chip: int
    app_end: int

    @property
    def body_max(self):
        return self.mark_sec - BODY_OFF

    @property
    def mark_sec(self):
        return self.app_end - MARK_TAIL

    @property
    def mark_off(self):
        return self.mark_sec + MARK_SEC_OFF

    @property
    def app_slot(self):
        return self.app_end - APP_LO - APP_SLOT_OVERHEAD

    @property
    def shift(self):
        """how far this map's stores lie above the FM-1's"""
        return self.app_end - HW_APP_END

    @property
    def vm_len(self):
        return VM_LEN

    @property
    def data_lo(self):
        return self.app_end + STORE_A_LEN

    @property
    def data_hi(self):
        return self.data_lo + DATA_LEN

    @property
    def ota_lo(self):
        return self.data_hi

    @property
    def bank_ext_lo(self):
        return self.ota_lo + OTA_LEN

    @property
    def glob_lo(self):
        return self.bank_ext_lo + BANK_EXT_LEN + GLOB_GAP

    @property
    def keymac(self):
        return self.glob_lo + GLOB_LEN

    @property
    def smp_user_lo(self):
        return self.data_lo + SMP_USER_OFF

    @property
    def btif(self):
        return 0xE9000 + self.shift

    @property
    def usr(self):
        return 0xEA000 + self.shift

    @property
    def flash_hdr_size(self):
        return self.chip - SECTOR

    def defines(self):
        """-D flags for the boot stub and the loader (fm1_flash_map.h), which are built without config.h."""
        return [f"-DCONFIG_FLASH_CHIP_SIZE={self.chip:#x}", f"-DCONFIG_FLASH_APP_END={self.app_end:#x}"]

    def check(self):
        """The orderings fm1_flash_map.h asserts, so a bad value fails here with a message."""
        for name, v in (("chip size", self.chip), ("app area end", self.app_end)):
            if v % SECTOR:
                raise SystemExit(f"flash map: {name} {v:#x} is not sector aligned")
        if not (APP_LO < self.mark_sec < self.app_end):
            raise SystemExit("flash map: the app area is too small")
        if self.keymac + SECTOR > self.chip:
            raise SystemExit(f"flash map: the stores above the app area end at {self.keymac + SECTOR:#x}, "
                             f"past the {self.chip:#x} chip")
        if self.chip > 1 << 24 or 0x02000000 + self.chip - APP_LO > 0x04000000:
            raise SystemExit("flash map: the chip is not addressable")
        if SMP_USER_OFF + SMP_USER_N * SMP_USER_SIZE > DATA_LEN:
            raise SystemExit("flash map: the sample slots leave the main store")


def from_config_h(text):
    """The Layout of a generated config.h."""
    def value(name):
        m = re.search(r"^#define CONFIG_" + name + r" (\w+)$", text, re.M)
        if not m:
            raise SystemExit(f"flash map: CONFIG_{name} is missing from config.h")
        return int(m.group(1), 0)
    layout = Layout(value("FLASH_CHIP_SIZE"), value("FLASH_APP_END"))
    layout.check()
    return layout
