// license: GPL-2.0+
// copyright-holders: Dirk Best
/***************************************************************************

    Regnecentralen RC750 Partner

    16-bit office micro, sibling of the RC759 Piccoline (see rc759.cpp).
    Both derive from rc75x_state (see rc75x.h), which holds the shared
    80186 + 8259A + 8255 + 82730 + MM58167 + NVM + sound + keyboard core;
    this file adds the Partner-specific floppy/serial/mass-storage side.
    The Partner shipped with Concurrent DOS.

    This models the RC750/23 central unit (2x 1200 KB floppy, 512 KB RAM, no
    Winchester; DDHF Bits:30005001). It runs the diagnostic ROM (ROD398/399,
    "*** TEST, V.4.3 ***") through its selftest and boots the SW1500 disk (an
    early Concurrent CP/M-86, v2.0) from the floppy to the install menu and on
    to the command prompt (DIR lists files), all in the machine's real 9x14
    font. Concurrent DOS itself is untested here. Still flagged
    MACHINE_NOT_WORKING: the WD1797 read path is not yet robust across all disk
    formats/drives, and the SCSI adapter, 8087 and colour monitor are unemulated.

    HARDWARE (I/O ports verified by disassembling the boot ROM + the PARTNER
    Programmer's Guide v3, jun 1986; the 80186 / 8259A / 8255 / 82730 / MM58167
    / NVM / sound / keyboard core is shared with the RC759 Piccoline, rc75x.h):

      CPU            Intel 80186 @ 8 MHz     (2 DMA ch, 3 timers, PIC on-chip)
      Coprocessor    Intel 8087              optional (not emulated)
      Extra PIC      Intel 8259A             I/O 0x00, cascaded to 80186 INT0
      Keyboard       HLE serial kbd          I/O 0x20, 8259A IR1
      Sound          SN76489A                I/O 0x56
      RTC            MM58167 (32.768 kHz)    I/O 0x5a/0x5c, 8259A IR3
      CRT control    Intel 82730 text proc   irst 0x230, chan-attn 0x240; the
                                              32 KB pixel memory @ 0xF0000 is
                                              the 9x14 char generator (§4.1.2)
      Palette        32 cells x 2 IRGB nib   I/O 0x180-0x1be (even)
      PPI            Intel 8255              I/O 0x70-0x76 (port A out = NVM
                                              block select)
      NVM            256x4 CMOS, bank-sw      I/O 0x80-0xfe
      Floppy         WD1797 FDC (as FD1797)  I/O 0x200-0x207, ctrl latch 0x210,
                                              config/ready sense 0x220
      Printer        Centronics latches      I/O 0x250 (data) / 0x260 (control)
      Serial         Intel 8274 (dual)       I/O 0x2a0-0x2a7, via 80186 INT1
      Mass storage   SCSI host adapter       I/O 0x2c0 (not emulated)

    Differs from the Piccoline: WD1797 (not WD2797), a built-in 8274 dual
    serial + SCSI host adapter instead of the Piccoline's cassette / iSBX /
    micronet / DPC, an optional 8087, and the character generator in a pixel
    memory at 0xF0000 (the Piccoline instead keeps a soft font in vram @0xD0000).

    Drive A boots CCP/M-86 2.0 from SW1500 and DIR A: works.  Drive B reads
    still fail with WD1797 Record Not Found on some sectors (#49) despite the
    drive being correctly selected/loaded -- likely side-select or a
    head-position/track-register desync across the drive switch.

    TODO:
    - Fix the drive-B WD1797 read path (Record Not Found, #49); #45 tracks the
      general read-path robustness.
    - Wire the SCSI host adapter and the optional 8087.
    - Colour monitor: decode the 82730 palette-select attribute bits (#47).

***************************************************************************/

#include "emu.h"
#include "rc75x.h"

#include "machine/wd_fdc.h"
#include "machine/z80sio.h"
#include "imagedev/floppy.h"
#include "formats/rc75x_dsk.h"


namespace {


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

class rc750_state : public rc75x_state
{
public:
	rc750_state(const machine_config &mconfig, device_type type, const char *tag) :
		rc75x_state(mconfig, type, tag),
		m_fdc(*this, "fdc"),
		m_floppy(*this, "fdc:%u", 0),
		m_sio(*this, "sio")
	{ }

	void rc750(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<fd1797_device> m_fdc;
	required_device_array<floppy_connector, 2> m_floppy;
	required_device<i8274_device> m_sio;

	static void floppy_formats(format_registration &fr);

	uint8_t fdc_sense_r();

	// Centronics printer port latches at 0x250 (data) / 0x260 (control). The
	// selftest verifies these are read/write (error 24/25 = printer control/data
	// signal test). Modelled as plain readback latches so a bare machine passes.
	uint8_t prn_data_r();
	void prn_data_w(uint8_t data);
	uint8_t prn_ctrl_r();
	void prn_ctrl_w(uint8_t data);
	uint8_t m_prn_data = 0;
	uint8_t m_prn_ctrl = 0;

	// Partner FDC/system control latch at 0x210 (write) with status read-back
	// (the boot ROM writes 0x40|bits and later reads bits 2-3 to verify). This
	// is NOT the 82730 reset -- that lives at 0x230 on both Partner and
	// Piccoline. The latch selects the WD1797 floppy, spins the motor and picks
	// the data rate (single/double density) so the drive reports READY.
	uint8_t fdc_ctrl_r();
	void fdc_ctrl_w(uint8_t data);
	uint8_t m_fdc_ctrl = 0;

	uint8_t ppi_porta_r();
	uint8_t ppi_portb_r();
	void ppi_portc_w(uint8_t data);

	// The Partner selects the NVM block (0..3) by writing bits 6-7 of PPI port A
	// (io 0x70): the diagnostic ROM's address_block routine (called by every
	// nvm_read/nvm_write) does a read-modify-write there -- Programmer's Guide
	// 3.2, "write_nvm". Port A is an OUTPUT latch on the Partner (the selftest's
	// walking-bit test 18 writes port A and reads the value back), so capture
	// the block from the port-A output callback. The shared rc75x base instead
	// latches the block from PPI port C bits 4-5 (the Piccoline wiring), which
	// the Partner never drives, so without this the block stays wherever port C
	// last left it (2) and the checksum test reads block 2 three times -> ERR 19.
	void nvm_block_w(uint8_t data);

	// Partner NVM default image. The selftest requires sum(block 0,1,2) == 0xAA
	// (Programmer's Guide 3.2); byte 25 is the load device ('A'). The shared
	// rc75x nvram_init seeds a Piccoline image whose 96-byte sum is 0x2A, so the
	// Partner needs its own seed.
	void nvram_init_partner(nvram_device &nvram, void *data, size_t size);

	void rc750_io(address_map &map) ATTR_COLD;
	void rc750_map(address_map &map) ATTR_COLD;
};


//**************************************************************************
//  ADDRESS MAPS
//**************************************************************************

void rc750_state::rc750_map(address_map &map)
{
	// 512 KB base RAM (RC750/23 central unit spec, DDHF Bits:30005001). All four
	// Partner central-unit models (/20 /21 /22 /23) ship 512 KB; MF101 adds a
	// second 512 KB. (Was 384 KB, inherited from the RC759 Piccoline.)
	map(0x00000, 0x7ffff).ram();
	// The Partner selftest sets its stack up in segment F000 (SS=F000,
	// SP=8000) and clears F000:0000-F000:7FFF first, so it expects RAM at
	// 0xf0000-0xf7fff -- a different memory layout from the Piccoline.
	// (Found by disassembling the reset-time spin at F951:07A0.)
	// 32 KB pixel memory at F000:0000 (Partner Programmer's Guide v3, 4.1.2):
	// in alphanumeric mode the pixel blocks ARE the character generator -- the
	// display word's bits 0-9 address a 32-byte block here holding the glyph
	// (14 rows of one 9-px word, MSB = leftmost). The firmware loads the real
	// 9x14 font here at boot, so the renderer reads glyphs straight from it.
	map(0xf0000, 0xf7fff).ram().share("pixmem");
	map(0xe8000, 0xeffff).mirror(0x10000).rom().region("bios", 0);
}

void rc750_state::rc750_io(address_map &map)
{
	map.unmap_value_high();
	// shared core ports (verified against the Piccoline)
	map(0x000, 0x003).mirror(0x0c).rw(m_pic, FUNC(pic8259_device::read), FUNC(pic8259_device::write)).umask16(0x00ff);
	map(0x020, 0x020).r(m_kbd, FUNC(rc759_kbd_hle_device::read));
	map(0x056, 0x056).w(m_snd, FUNC(sn76494_device::write));
	map(0x056, 0x057).nopr();
	map(0x05a, 0x05a).w(FUNC(rc750_state::rtc_data_w));
	map(0x05c, 0x05c).rw(FUNC(rc750_state::rtc_data_r), FUNC(rc750_state::rtc_addr_w));
	map(0x070, 0x077).mirror(0x08).rw(m_ppi, FUNC(i8255_device::read), FUNC(i8255_device::write)).umask16(0x00ff);
	map(0x080, 0x0ff).rw(FUNC(rc750_state::nvram_r), FUNC(rc750_state::nvram_w)).umask16(0x00ff);
	map(0x180, 0x1bf).rw(FUNC(rc750_state::palette_r), FUNC(rc750_state::palette_w)).umask16(0x00ff);
	// 82730 CRT: like the Piccoline the Partner drives channel-attention at
	// 0x240 (mailbox handshake at F000:2000, seen at F951:08DA "out 240h"), and
	// the 82730 reset/interrupt-ack (irst) at 0x230 -- same port as the
	// Piccoline. The boot ROM's 82730 SINT handler writes 0x230 to ack each
	// frame interrupt; the mailbox handshake is kicked by the 0x240 CA.
	map(0x230, 0x231).w(FUNC(rc750_state::txt_irst_w));
	map(0x240, 0x241).w(FUNC(rc750_state::txt_ca_w));
	// 0x210 is the FDC/system control latch (NOT the 82730 reset -- that guess
	// mis-routed the WD1797's drive-select/motor writes to the 82730, so the
	// drive never became ready and the selftest hung after the banner). The ROM
	// writes 0x40|<bits> here (0x42/0x45 pick the data rate, 0x46/0x47 during
	// drive detect) and reads bits 2-3 back to verify.
	map(0x210, 0x211).rw(FUNC(rc750_state::fdc_ctrl_r), FUNC(rc750_state::fdc_ctrl_w)).umask16(0x00ff);
	// WD1797 FDC at 0x200-0x206 (status/cmd, track, sector, data on even bytes).
	// Verified by disassembling the boot ROM: the selftest polls 0x200 bit0 (WD
	// BUSY) at FD72E and writes the Force-Interrupt command 0xD8 to 0x200. The
	// earlier 0x280 guess (from the Piccoline) was wrong for the Partner.
	map(0x200, 0x207).rw(m_fdc, FUNC(fd1797_device::read), FUNC(fd1797_device::write)).umask16(0x00ff);
	// Floppy sense input (config jumpers in bits 6-7, ready in bit4; active low).
	map(0x220, 0x221).r(FUNC(rc750_state::fdc_sense_r)).umask16(0x00ff);
	// Centronics printer port: data register 0x250, control register 0x260.
	// The selftest's "printer port" test (service manual Bits:30002753, error 24
	// = control signals, error 25 = data signals) writes a walking-bit pattern
	// to 0x250 and expects it to read back, and writes 0x260 checking bits 5-6
	// read back -- i.e. the port latches must be read/write. A working port
	// reads its own latch back (the readback lines are internal to the latch,
	// not gated on an attached printer), so model both as plain readback
	// latches; leaving them unmapped simulated a broken port and hung POST at
	// error 25 after the banner.
	map(0x250, 0x251).rw(FUNC(rc750_state::prn_data_r), FUNC(rc750_state::prn_data_w)).umask16(0x00ff);
	map(0x260, 0x261).rw(FUNC(rc750_state::prn_ctrl_r), FUNC(rc750_state::prn_ctrl_w)).umask16(0x00ff);
	map(0x2a0, 0x2a7).rw(m_sio, FUNC(i8274_device::ba_cd_r), FUNC(i8274_device::ba_cd_w)).umask16(0x00ff);
//  map(0x2c0, 0x2cf) SCSI host adapter (not emulated)
}


//**************************************************************************
//  INPUT DEFINITIONS
//**************************************************************************

static INPUT_PORTS_START( rc750 )
	PORT_START("config")
	// The Partner shipped with a monochrome amber (P3) monitor. Defaulting to
	// "Color" makes the firmware treat the display-buffer attribute byte as
	// foreground/background colour nibbles, so text with fg colour 0 renders
	// invisible and the intensity/bold bit is lost. Default to Monochrome so the
	// attribute byte is interpreted as mono intensity/highlight instead.
	PORT_CONFNAME(0x20, 0x20, "Monitor Type")
	PORT_CONFSETTING(0x00, "Color")
	PORT_CONFSETTING(0x20, "Monochrome")
	PORT_CONFNAME(0x40, 0x00, "Monitor Frequency")
	PORT_CONFSETTING(0x00, "15 kHz")
	PORT_CONFSETTING(0x40, "22 kHz")
INPUT_PORTS_END


//**************************************************************************
//  FLOPPY
//**************************************************************************

void rc750_state::floppy_formats(format_registration &fr)
{
	fr.add_mfm_containers();
	fr.add(FLOPPY_RC75X_FORMAT);
}

void rc750_state::prn_data_w(uint8_t data)
{
	// Centronics printer data latch at 0x250. The selftest writes a walking-bit
	// pattern and reads it straight back (error 25 = data-signal test).
	m_prn_data = data;
}

uint8_t rc750_state::prn_data_r()
{
	return m_prn_data;
}

void rc750_state::prn_ctrl_w(uint8_t data)
{
	// Centronics printer control latch at 0x260. The selftest writes control
	// codes and checks bits 5-6 read back (error 24 = control-signal test).
	m_prn_ctrl = data;
}

uint8_t rc750_state::prn_ctrl_r()
{
	return m_prn_ctrl;
}

uint8_t rc750_state::fdc_sense_r()
{
	// Configuration / device-presence sense input at 0x220 (active low). Bits 6-7
	// are the monitor config jumpers (read inverted at f9e17). Bits 5-0 are a
	// device-presence field scanned at fa8fe (a 0 bit = device installed). The
	// selftest gates the local-network test on bit4 (fb0ad: `in 220h; not al;
	// test al,10h; jne run-test`) -- bit4 = 0 means "LAN card present" and makes
	// the test wait for a network interrupt that a bare machine can't produce
	// (ERROR 39). This machine has no local-network card and no Winchester, so
	// report every device absent (all presence bits high) while keeping the
	// monitor jumpers (bits 6-7) at their working value: 0xff.
	return 0xff;
}

void rc750_state::fdc_ctrl_w(uint8_t data)
{
	// FDC/system control latch at 0x210.  Bit6 = FDC enable/motor (always set on
	// the active writes 0x42/0x45/0x46/0x47 for drive A and 0x4a/0x4b/0x4e/0x4f
	// for drive B), bit3 = drive select (0 = drive A, 1 = drive B -- found by
	// tracing 0x210 during DIR A: vs DIR B:), bit0 = data-rate/density select.
	m_fdc_ctrl = data;

	floppy_image_device *fl = m_floppy[BIT(data, 3)]->get_device();
	m_fdc->set_floppy(fl);
	if (fl)
		fl->mon_w(BIT(data, 6) ? 0 : 1); // motor on while the FDC is enabled

	m_fdc->dden_w(BIT(data, 0)); // single/double density

	// NOTE: drive B still gets Record Not Found on some reads (#49); side-select
	// and per-drive head position across the switch are the prime suspects.
}

uint8_t rc750_state::fdc_ctrl_r()
{
	// The ROM reads 0x210 back and masks bits 2-3 to verify the last write took
	// effect; return the latched value so write-then-verify is consistent.
	return m_fdc_ctrl;
}


//**************************************************************************
//  I/O (PPI)
//**************************************************************************

uint8_t rc750_state::ppi_porta_r()
{
	// PROVISIONAL. The Partner has no cassette/iSBX/DPC, so those inputs
	// (bits 0-3, 6 on the Piccoline) read as "not installed"; the memory
	// ident bits are kept the same as the working Piccoline setup.
	uint8_t data = 0;

	data |= 0 << 0; // no cassette input
	data |= 0 << 1; // no iSBX present
	data |= 0 << 2; // no iSBX opt0
	data |= 0 << 3; // no iSBX opt1
	data |= 1 << 4; // mem ident0
	data |= 0 << 5; // mem ident1 (bit4=1,bit5=0 = 384k installed)
	data |= 0 << 6; // dpc connect (0 = external floppy installed)
	data |= 1 << 7; // not used

	return data;
}

uint8_t rc750_state::ppi_portb_r()
{
	// PROVISIONAL, adapted from the Piccoline port B.
	uint8_t data = 0;

	data |= 1 << 0; // no micronet controller
	data |= 1 << 1; // rtc type
	data |= m_snd->ready_r() << 2;
	data |= 0 << 3; // SCSI handshake line: POST test 35 polls this bit (in 0x72,
	                // bit3) to go low with a timeout; on the Piccoline bit3 was
	                // "not used" and read 1, which timed out -> ERROR 00035.
	data |= 1 << 4; // not used
	data |= m_config->read(); // monitor type and frequency
	data |= 1 << 7; // not used

	return data;
}

void rc750_state::ppi_portc_w(uint8_t data)
{
	// 7-------  keyboard enable
	// -6------  gfx mode
	// --54----  (Piccoline nvram bank; the Partner selects the block via port A
	//            instead -- see nvm_block_w -- so do NOT latch the block here)
	// ----32--  drq source
	// ------10  unused on the Partner (cassette on the Piccoline)

	m_kbd->enable_w(BIT(data, 7));
	m_drq_source = (data >> 2) & 0x03;
}

void rc750_state::nvm_block_w(uint8_t data)
{
	// PPI port-A output callback. address_block writes bits 6-7 = NVM block
	// number (0..3); the 8255 itself handles the walking-bit readback (test 18).
	m_nvram_bank = (data >> 6) & 0x03;
}

void rc750_state::nvram_init_partner(nvram_device &nvram, void *data, size_t size)
{
	// Blank Partner NVM with a valid system-parameter checksum. The selftest
	// (Programmer's Guide 3.2) sums NVM blocks 0,1,2 (96 bytes) modulo 256 and
	// requires 0xAA. Byte 25 is the load device ('A' = boot from floppy drive
	// A); byte 0 is the checksum byte, chosen so the 96-byte sum is 0xAA
	// (0xAA - 0x41 = 0x69). All other parameters default to 0.
	memset(data, 0x00, size);
	uint8_t *p = reinterpret_cast<uint8_t *>(data);
	if (size > 25)
	{
		p[25] = 0x41; // load device = 'A'
		p[0]  = 0x69; // checksum byte: 0x69 + 0x41 == 0xAA
	}
}


//**************************************************************************
//  MACHINE
//**************************************************************************

void rc750_state::machine_start()
{
	rc75x_state::machine_start();

	// Character generator. The real 9x14 font is the pixel memory at 0xF0000:
	// the boot ROM loads the full standard font (upper- and lower-case + frame
	// glyphs) there at POST -- a routine at ROM offset 0x1CE2 (runs @F9CE2,
	// ~t=0.85s, right after it RAM-tests the pixel memory) copies the glyphs in,
	// so it is present before the banner and long before any disk boots. The
	// renderer reads glyphs straight from it via m_pixmem (see txt_update_row);
	// the CCP/M XIOS INT-28h define_font (AL=52) call only edits it at runtime
	// (soft fonts / 4 alternative banks, Programmer's Guide 4.3) and is not
	// needed here. init_rom_font is kept ONLY to select the Partner ROM-font
	// render path (m_use_rom_font) -- its glyph table (47 upper-case records at
	// ROM offset 0x7f, format [ASCII][15x 7-px rows, bit6=left][pad]) is just a
	// fallback for any character not yet defined in the pixel memory.
	init_rom_font(memregion("bios")->base() + 0x7f, 47, 17, 1);

	// 80 columns across the 720 px active field (mode block hfldstrt=13,
	// hfldstp=58, *16) -> 9 px cell pitch; the 7 px glyph leaves a 2 px gap.
	m_text_hpitch = 9;
}


//**************************************************************************
//  MACHINE DEFINITIONS
//**************************************************************************

static void rc750_floppies(device_slot_interface &device)
{
	device.option_add("hd", FLOPPY_525_HD);
}

void rc750_state::rc750(machine_config &config)
{
	I80186(config, m_maincpu, 8'000'000);
	m_maincpu->set_addrmap(AS_PROGRAM, &rc750_state::rc750_map);
	m_maincpu->set_addrmap(AS_IO, &rc750_state::rc750_io);

	// shared 80186/8259/8255-slave/82730/rtc/nvm/sound/keyboard core
	add_common_devices(config);

	// The shared core seeds a Piccoline NVM image (96-byte sum 0x2A); the
	// Partner selftest wants sum 0xAA, so override the seed with a Partner image.
	m_nvram->set_custom_handler(FUNC(rc750_state::nvram_init_partner));

	I8255(config, m_ppi);
	m_ppi->in_pa_callback().set(FUNC(rc750_state::ppi_porta_r));
	m_ppi->out_pa_callback().set(FUNC(rc750_state::nvm_block_w));
	m_ppi->in_pb_callback().set(FUNC(rc750_state::ppi_portb_r));
	m_ppi->out_pc_callback().set(FUNC(rc750_state::ppi_portc_w));

	// floppy (WD1797, modelled as FD1797)
	FD1797(config, m_fdc, 2'000'000);
	m_fdc->intrq_wr_callback().set(m_pic, FUNC(pic8259_device::ir0_w));
	m_fdc->drq_wr_callback().set(m_maincpu, FUNC(i80186_cpu_device::drq0_w));

	FLOPPY_CONNECTOR(config, "fdc:0", rc750_floppies, "hd", rc750_state::floppy_formats);
	FLOPPY_CONNECTOR(config, "fdc:1", rc750_floppies, "hd", rc750_state::floppy_formats);

	// dual serial (Intel 8274); routed to the 80186 INT1 per the guide's
	// interrupt structure. Serial lines are not wired up yet.
	I8274(config, m_sio, 4'000'000);
	m_sio->out_int_callback().set(m_maincpu, FUNC(i80186_cpu_device::int1_w));
}


//**************************************************************************
//  ROM DEFINITIONS
//**************************************************************************

ROM_START( rc750 )
	// Partner boot ROM: two byte-wide 16 KB EPROMs on the 16-bit 80186 bus.
	// ROD398 = even/low byte, ROD399 = odd/high byte.  Interleaved this gives
	// the reset vector at 0xffff0 as FA EA 00 00 F8 FF (CLI; JMP FFF8:0000),
	// matching the RC759 layout.
	ROM_REGION16_LE(0x8000, "bios", 0)
	ROM_LOAD16_BYTE("rod398.bin", 0x0000, 0x4000, CRC(37bb9bf8) SHA1(bc9ea2faf38bcff7b0cf6cd4382908758a187938))
	ROM_LOAD16_BYTE("rod399.bin", 0x0001, 0x4000, CRC(53c8b085) SHA1(53bfa9d77549281aa8266cb3a20b0aa84a0e6dc4))
ROM_END


} // anonymous namespace


//**************************************************************************
//  SYSTEM DRIVERS
//**************************************************************************

//    YEAR  NAME   PARENT  COMPAT  MACHINE  INPUT  CLASS        INIT        COMPANY           FULLNAME         FLAGS
// This models the RC750/23 central unit: 2x 1200 KB floppy, 512 KB RAM, no
// Winchester (DDHF Bits:30005001). The /20 adds a 20 MB WD, /22 has one floppy,
// /21 none -- all otherwise identical (same 80186/ROM/512 KB base).
COMP( 1985, rc750, 0,      0,      rc750,   rc750, rc750_state, empty_init, "Regnecentralen", "RC750/23 Partner", MACHINE_SUPPORTS_SAVE )
