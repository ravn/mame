// license:BSD-3-Clause
// copyright-holders:Robbbert
/******************************************************************************************************************

Regnecentralen Piccolo RC702/RC703

2016-09-10 Skeleton driver

Undumped prom at IC55 type 74S287 (address decoder for PROM0/PROM1 mapping)
Keyboard has 8048 and 2758, both undumped.

Machine variants:
  rc702      - RC702 with 8" DSDD floppy drives (maxi), 8 MHz FDC
  rc702mini  - RC702 with 5.25" DD floppy drives (mini), 4 MHz FDC
  rc703      - RC703 with 5.25" QD floppy drives (80-track), 4 MHz FDC


ToDo:
- Hard drive for RC703, ports 0x60-0x67. Extra CTC on HD board, ports 0x44-0x47
- Keyboard MCU (8048 + 2758) — currently using generic_keyboard

PIO peripherals (J3 / J4):
- PIO-A (J4): keyboard, wired direct (generic_keyboard -> kbd_put -> in_pa_callback).
  Originally a slot device too — reverted to direct wiring as a workaround for
  ravn/mame#6 (two slot devices on a single Z80-PIO chip break IM2 IRQ delivery
  in MAME's flat z80pio_device model).  Matches Einstein's proven topology
  (Port A direct, Port B via slot).
- PIO-B (J3): exposed as a configurable slot device via bus/rc702/pio_port/.
  Default empty (matches factory state — J3 was an unpopulated expansion
  connector with no power rail, see RC702 Technical Reference).
- Note: PIO-B is *not* the printer port; the printer was always on a SIO
  channel per the hardware reference and the standard CP/M IOBYTE mapping.
- prom1 (line program ROM) is undumped; the region is filled with 0xff to avoid a missing-ROM
  warning.

****************************************************************************************************************/

#include "emu.h"

#include "bus/rc702/pio_port/pio_port.h"
#include "bus/rs232/rs232.h"
#include "cpu/z80/z80.h"
#include "imagedev/floppy.h"
#include "machine/z80daisy.h"
#include "machine/7474.h"
#include "machine/am9517a.h"
#include "machine/clock.h"
#include "machine/keyboard.h"
#include "machine/upd765.h"
#include "machine/z80ctc.h"
#include "machine/z80pio.h"
#include "machine/z80sio.h"
#include "sound/beep.h"
#include "video/i8275.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"

#include "rc702.lh"


namespace {

class rc702_state : public driver_device
{
public:
	rc702_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_palette(*this, "palette")
		, m_maincpu(*this, "maincpu")
		, m_rom(*this, "maincpu")
		, m_rom_prom1(*this, "prom1")
		, m_ram(*this, "mainram")
		, m_bank1(*this, "bank1")
		, m_bank1h(*this, "bank1h")
		, m_bank2(*this, "bank2")
		, m_bank2h(*this, "bank2h")
		, m_p_chargen(*this, "chargen")
		, m_ctc1(*this, "ctc1")
		, m_pio(*this, "pio")
		, m_dma(*this, "dma")
		, m_beep(*this, "beeper")
		, m_7474(*this, "7474")
		, m_fdc(*this, "fdc")
		, m_floppy0(*this, "fdc:0")
		, m_rs232a(*this, "rs232a")
		, m_rs232b(*this, "rs232b")
		, m_pio_b(*this, "piob")
	{ }

	void rc700_base(machine_config &config);
	void rc702(machine_config &config);
	void rc702mini(machine_config &config);
	void rc703(machine_config &config);

	void rc702sem702(machine_config &config);

protected:
	virtual void machine_start() override ATTR_COLD;
	virtual void machine_reset() override ATTR_COLD;

private:
	uint8_t memory_read_byte(offs_t offset);
	void memory_write_byte(offs_t offset, uint8_t data);
	void port14_w(uint8_t data);
	void port1c_w(uint8_t data);
	void sem702_char_w(uint8_t data);
	void sem702_dot_w(uint8_t data);
	void sem702_data_w(uint8_t data);
	void crtc_drq_w(int state);
	void hreq_w(int state);
	void clock_w(int state);
	void eop_w(int state);
	void q_w(int state);
	void qbar_w(int state);
	void dack1_w(int state);
	void dack2_w(int state);
	I8275_DRAW_CHARACTER_MEMBER(display_pixels);
	void rc702_palette(palette_device &palette) const;
	void kbd_put(u8 data);
	uint8_t kbd_r();
	uint8_t m_kbd_data = 0U;

	void io_map(address_map &map) ATTR_COLD;
	void mem_map(address_map &map) ATTR_COLD;

	bool m_q_state = false;
	bool m_qbar_state = false;
	bool m_drq_state = false;
	uint16_t m_beepcnt = 0U;
	bool m_eop = false;
	bool m_dack1 = false;
	// Real-line voltage of 8237 DACK2; HIGH (=1) means ch2 inactive.
	// Fed alongside m_eop into the 74LS32 OR gate on MIC 11 whose output
	// clocks the 74LS74 that selects between DMA ch2 and ch3 for the
	// 8275 (roll-function second channel).  See
	// rc700-gensmedet/docs/dma_ch3_8275_roll_function.md.
	bool m_dack2 = true;
	// SEM702 RAM-based character generator (IC82 swap-in for ROA327).
	// 128 chars * 16 lines = 2 KB.  Uninitialised at power-on on real
	// hardware; we fill with 0xFF so an un-programmed boot is visually
	// obvious (all-dots glyphs).  Programmed via OUT ports 0xD1/0xD2/0xD3.
	uint8_t m_sem702_ram[0x800] = {};
	uint8_t m_sem702_char_latch = 0U;
	uint8_t m_sem702_dot_latch = 0U;
	bool m_has_sem702 = false;
	required_device<palette_device> m_palette;
	required_device<z80_device> m_maincpu;
	required_region_ptr<u8> m_rom;
	required_region_ptr<u8> m_rom_prom1;
	required_shared_ptr<u8> m_ram;
	required_memory_bank    m_bank1;
	required_memory_bank    m_bank1h;
	required_memory_bank    m_bank2;
	required_memory_bank    m_bank2h;
	required_region_ptr<u8> m_p_chargen;
	required_device<z80ctc_device> m_ctc1;
	required_device<z80pio_device> m_pio;
	required_device<am9517a_device> m_dma;
	required_device<beep_device> m_beep;
	required_device<ttl7474_device> m_7474;
	required_device<upd765a_device> m_fdc;
	required_device<floppy_connector> m_floppy0;
	required_device<rs232_port_device> m_rs232a;
	required_device<rs232_port_device> m_rs232b;
	required_device<rc702_pio_port_device> m_pio_b;
};


void rc702_state::mem_map(address_map &map)
{
	map(0x0000, 0xffff).ram().share("mainram");
	map(0x0000, 0x07ff).bankr("bank1");
	map(0x0800, 0x0fff).bankr("bank1h");
	map(0x2000, 0x27ff).bankr("bank2");
	map(0x2800, 0x2fff).bankr("bank2h");
}

void rc702_state::io_map(address_map &map)
{
	map.global_mask(0xff);
	map.unmap_value_high();
	map(0x00, 0x01).rw("crtc", FUNC(i8275_device::read), FUNC(i8275_device::write));
	map(0x04, 0x05).m(m_fdc, FUNC(upd765a_device::map));
	map(0x08, 0x0b).rw("sio1", FUNC(z80sio_device::cd_ba_r), FUNC(z80sio_device::cd_ba_w)); // boot sequence doesn't program this
	map(0x0c, 0x0f).rw(m_ctc1, FUNC(z80ctc_device::read), FUNC(z80ctc_device::write));
	map(0x10, 0x13).rw(m_pio, FUNC(z80pio_device::read), FUNC(z80pio_device::write));
	map(0x14, 0x17).portr("DSW").w(FUNC(rc702_state::port14_w)); // motors
	map(0x18, 0x1b).lw8(NAME([this] (u8 data) { m_bank1->set_entry(0); m_bank1h->set_entry(0); m_bank2->set_entry(0); m_bank2h->set_entry(0); })); // replace roms with ram
	map(0x1c, 0x1f).w(FUNC(rc702_state::port1c_w)); // sound
	// SEM702 RAM-based character generator (IC82).  Handlers are wired
	// on every rc702 variant but only act when m_has_sem702 is set --
	// matching real hardware where these ports go nowhere on machines
	// with the original ROA327 fixed-font ROM installed.
	map(0xd1, 0xd1).w(FUNC(rc702_state::sem702_char_w));
	map(0xd2, 0xd2).w(FUNC(rc702_state::sem702_dot_w));
	map(0xd3, 0xd3).w(FUNC(rc702_state::sem702_data_w));
	map(0xf0, 0xff).rw(m_dma, FUNC(am9517a_device::read), FUNC(am9517a_device::write));
}

// PROM socket jumpers: select 2716 (2KB) or 2732 (4KB) EPROM.
// Pin 21 of the EPROM socket is jumpered to either +5V (Vpp for 2716)
// or address line A11 (for 2732).  See RC702 technical manual page 63.
static INPUT_PORTS_START( rc702_promcfg )
	PORT_START("PROMCFG")
	PORT_CONFNAME( 0x01, 0x00, "PROM0 (IC66) Type")
	PORT_CONFSETTING(    0x00, "2716 (2KB)")
	PORT_CONFSETTING(    0x01, "2732 (4KB)")
	PORT_CONFNAME( 0x02, 0x02, "PROM1 (IC65) Type")
	PORT_CONFSETTING(    0x00, "2716 (2KB)")
	PORT_CONFSETTING(    0x02, "2732 (4KB)")
INPUT_PORTS_END

/* Input ports - PROM reads port 0x14 bit 7: set=mini, clear=maxi.
 * Reconstructed-firmware uses for the SW1 bits (canonical doc is
 * rc700-gensmedet/docs/SW1_BIT_MAP.md):
 *   bit 0 (S01): console mode -- On (bit=0, default) = joined
 *                (SIO-B + keyboard input, SIO-B + CRT output);
 *                Off (bit=1) = local CRT + keyboard only.  Both
 *                rcbios-in-c and cpnos-in-c honour this at cold boot.
 *   bit 1 (S02): PROM1 lineprog enable -- On (bit=0, default) =
 *                autoload checks PROM1 signature on floppy-boot
 *                failure and jumps if present; Off (bit=1) = skip
 *                the check, halt with NO DISKETTE NOR LINEPROG.
 *                No longer gates chargen loading (autoload's
 *                define_sextants runs unconditionally since 2026-05-17).
 *   bit 2 (S03): cpnos transport (PROM1-only lineprog build) --
 *                On (bit=0, default) = PIO-B (IRQ, 256 B ring),
 *                Off (bit=1) = SIO-A (38400 polled).  Both linked,
 *                jump-table patched at cold-init.  Ignored on
 *                two-PROM cpnos builds.
 *   bit 7 (S08): mini/maxi floppy (original-RC702 hardware bit). */
static INPUT_PORTS_START( rc702_maxi )
	PORT_INCLUDE( rc702_promcfg )
	PORT_START("DSW")
	PORT_DIPNAME( 0x01, 0x00, "S01 SIO-B console (rcbios)")
	PORT_DIPSETTING(    0x01, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x02, 0x00, "S02 PROM1=lineprog")
	PORT_DIPSETTING(    0x02, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x04, 0x00, "S03 cpnos transport (On=PIO, Off=SIO)")
	PORT_DIPSETTING(    0x04, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x08, 0x00, "S04")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x10, 0x00, "S05")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x20, 0x00, "S06")
	PORT_DIPSETTING(    0x20, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x40, 0x00, "S07")
	PORT_DIPSETTING(    0x40, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x80, 0x00, "S08 Minifloppy")
	PORT_DIPSETTING(    0x80, DEF_STR( On ))
	PORT_DIPSETTING(    0x00, DEF_STR( Off ))
INPUT_PORTS_END

/* See rc702_maxi comment for the SW1 bit assignments. */
static INPUT_PORTS_START( rc702_mini )
	PORT_INCLUDE( rc702_promcfg )
	PORT_START("DSW")
	PORT_DIPNAME( 0x01, 0x00, "S01 SIO-B console (rcbios)")
	PORT_DIPSETTING(    0x01, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x02, 0x00, "S02 PROM1=lineprog")
	PORT_DIPSETTING(    0x02, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x04, 0x00, "S03 cpnos transport (On=PIO, Off=SIO)")
	PORT_DIPSETTING(    0x04, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x08, 0x00, "S04")
	PORT_DIPSETTING(    0x08, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x10, 0x00, "S05")
	PORT_DIPSETTING(    0x10, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x20, 0x00, "S06")
	PORT_DIPSETTING(    0x20, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x40, 0x00, "S07")
	PORT_DIPSETTING(    0x40, DEF_STR( Off ))
	PORT_DIPSETTING(    0x00, DEF_STR( On ))
	PORT_DIPNAME( 0x80, 0x80, "S08 Minifloppy")
	PORT_DIPSETTING(    0x80, DEF_STR( On ))
	PORT_DIPSETTING(    0x00, DEF_STR( Off ))
INPUT_PORTS_END

void rc702_state::machine_reset()
{
	// PROM lower halves always map to ROM
	m_bank1->set_entry(1);
	m_bank2->set_entry(1);

	// PROM upper halves depend on jumper setting:
	// 2716 (2KB): A11 not connected — upper half mirrors lower half
	// 2732 (4KB): A11 active — upper half is distinct ROM data
	uint8_t promcfg = ioport("PROMCFG")->read();
	m_bank1h->set_entry(BIT(promcfg, 0) ? 2 : 1);  // 4K: ROM+0x800, 2K: mirror
	m_bank2h->set_entry(BIT(promcfg, 1) ? 2 : 1);  // 4K: ROM+0x800, 2K: mirror

	m_beepcnt = 0xffff;
	m_dack1 = 0;
	m_eop = 0;
	// ch2 DACK starts inactive (real line HIGH); along with m_eop=0
	// (TC inactive after init) keeps the 74LS32 OR output HIGH at reset.
	m_dack2 = true;
	m_7474->preset_w(1);
	m_7474->d_w(1);          // D input tied to LOGICAL ONE (R35 pull-up)
	m_7474->clock_w(1);      // OR-gate output is HIGH at reset

	// Set FDC data rate: 8" maxi drives use 500 kbps, 5.25" mini use 250 kbps.
	// DIP switch S08 bit 7: clear = maxi (8"), set = mini (5.25").
	m_fdc->set_rate(BIT(ioport("DSW")->read(), 7) ? 250000 : 500000);

	m_maincpu->reset();
}

void rc702_state::machine_start()
{
	// Lower halves: entry 0 = RAM, entry 1 = ROM
	m_bank1->configure_entry(0, m_ram);
	m_bank1->configure_entry(1, m_rom);
	m_bank2->configure_entry(0, &m_ram[0x2000]);
	m_bank2->configure_entry(1, m_rom_prom1);

	// Upper halves: entry 0 = RAM, entry 1 = mirror of lower ROM (2716), entry 2 = upper ROM (2732)
	m_bank1h->configure_entry(0, &m_ram[0x0800]);
	m_bank1h->configure_entry(1, &m_rom[0x0000]);        // 2KB mirror
	m_bank1h->configure_entry(2, &m_rom[0x0800]);        // 4KB upper half
	m_bank2h->configure_entry(0, &m_ram[0x2800]);
	m_bank2h->configure_entry(1, &m_rom_prom1[0x0000]);  // 2KB mirror
	m_bank2h->configure_entry(2, &m_rom_prom1[0x0800]);  // 4KB upper half
	save_item(NAME(m_q_state));
	save_item(NAME(m_qbar_state));
	save_item(NAME(m_drq_state));
	save_item(NAME(m_beepcnt));
	save_item(NAME(m_eop));
	save_item(NAME(m_dack1));
	save_item(NAME(m_dack2));
	save_item(NAME(m_kbd_data));

	if (m_has_sem702)
	{
		// Power-on SEM702 RAM is undefined.  Initialise to 0xFF so an
		// un-programmed boot shows solid blocks (loudly "no font loaded")
		// rather than blank screen that could be mistaken for a working
		// display.  Software is expected to overwrite this before
		// enabling the CRT.
		std::memset(m_sem702_ram, 0xff, sizeof(m_sem702_ram));
		save_item(NAME(m_sem702_ram));
		save_item(NAME(m_sem702_char_latch));
		save_item(NAME(m_sem702_dot_latch));
	}
}

void rc702_state::q_w(int state)
{
	m_q_state = state;

	if (m_q_state && m_drq_state)
		m_dma->dreq3_w(1);
	else
		m_dma->dreq3_w(0);
}

void rc702_state::qbar_w(int state)
{
	m_qbar_state = state;

	if (m_qbar_state && m_drq_state)
		m_dma->dreq2_w(1);
	else
		m_dma->dreq2_w(0);
}

void rc702_state::crtc_drq_w(int state)
{
	m_drq_state = state;

	if (m_q_state && m_drq_state)
		m_dma->dreq3_w(1);
	else
		m_dma->dreq3_w(0);

	if (m_qbar_state && m_drq_state)
		m_dma->dreq2_w(1);
	else
		m_dma->dreq2_w(0);
}

void rc702_state::eop_w(int state)
{
	if (state == m_eop)
		return;

	m_eop = state;

	if (!m_eop && !m_dack1)
		m_fdc->tc_w(1);
	else
		m_fdc->tc_w(0);

	// 74LS32 OR gate on MIC 11: inputs = /DACK2 + /TC (real-line voltages,
	// active-low).  Output is LOW only when both are simultaneously active;
	// rising edge at end of TC pulse clocks the 74LS74 with D=1, switching
	// CRTC DRQ routing from ch2 to ch3 (the "roll function" — see
	// rc700-gensmedet/docs/dma_ch3_8275_roll_function.md).
	m_7474->clock_w(m_dack2 || m_eop);
}

void rc702_state::dack1_w(int state)
{
	if (state == m_dack1)
		return;

	m_dack1 = state;

	if (!m_eop && !m_dack1)
		m_fdc->tc_w(1);
	else
		m_fdc->tc_w(0);

	//m_fdc->dack_w = state;  // pin not emulated
}

void rc702_state::dack2_w(int state)
{
	if (state == m_dack2)
		return;

	m_dack2 = state;

	// 74LS32 OR gate on MIC 11 -- see comment in eop_w() above.
	m_7474->clock_w(m_dack2 || m_eop);
}

void rc702_state::port14_w(uint8_t data)
{
	// Mini floppy motor control: bit 0 = 1 starts motor, 0 stops it.
	// Maxi (8") drives have always-spinning motors so mon_w() is a no-op.
	// Do NOT call set_floppy() here — the FDC connector already binds flopi[0]
	// during device_start().  Calling set_floppy() would assign the same device
	// to all 4 internal FDC drive slots, causing 4 spurious ready-change
	// interrupts on a single drive event and deadlocking the boot PROM.
	floppy_image_device *floppy = m_floppy0->get_device();
	if (floppy)
		floppy->mon_w(!BIT(data, 0));
}

void rc702_state::port1c_w(uint8_t data)
{
	m_beep->set_state(1);
	m_beepcnt = 0x3000;
}

// SEM702 character generator: writes are accepted only when the variant
// flagged m_has_sem702.  This matches real hardware where ports
// 0xD1/0xD2/0xD3 land on a SEM702 RAM board fitted in IC82, and go
// nowhere on machines that still have the ROA327 font ROM there.
//
// Modelled as three pure latches with no side effects: 0xD1 latches the
// 7-bit character address (ACHAR), 0xD2 the 4-bit line address (ALINE),
// 0xD3 writes RAM[(ACHAR << 4) | ALINE].  Real SEM702 hardware behaviour
// is not yet observed; both software sources we have (autoload's
// define_sextants and the Comal80 example in docs/RC702tech.txt) set
// ALINE explicitly before every byte, so we have no evidence of any
// auto-increment.  Keeping MAME's model strict avoids letting future
// software accidentally rely on side effects that may not exist on the
// physical board.
void rc702_state::sem702_char_w(uint8_t data)
{
	if (!m_has_sem702) return;
	m_sem702_char_latch = data & 0x7f;
}

void rc702_state::sem702_dot_w(uint8_t data)
{
	if (!m_has_sem702) return;
	m_sem702_dot_latch = data & 0x0f;
}

void rc702_state::sem702_data_w(uint8_t data)
{
	if (!m_has_sem702) return;
	uint16_t addr = (uint16_t(m_sem702_char_latch) << 4) | m_sem702_dot_latch;
	m_sem702_ram[addr & 0x7ff] = data;
}

// monitor is orange even when powered off
void rc702_state::rc702_palette(palette_device &palette) const
{
	// Colours sampled from the jbox (Michael Ringgard) RC702 emulator, which
	// matches the RC752 (NEC JB-1201M(A)) amber monitor: a dark warm-brown
	// background with a soft amber foreground -- not the earlier bright
	// saturated orange (which lit the whole screen too hot).
	palette.set_pen_color(0, rgb_t(0x4f, 0x25, 0x09));  // background: dark brown
	palette.set_pen_color(1, rgb_t(0xc4, 0x9b, 0x47));  // foreground: soft amber
}

I8275_DRAW_CHARACTER_MEMBER( rc702_state::display_pixels )
{
	const rgb_t *palette = m_palette->palette()->entry_list_raw();
	uint8_t gfx = 0;

	using namespace i8275_attributes;

	if (!BIT(attrcode, VSP))
	{
		// GPA0 from field attribute selects ROA327 (semigraphics) vs ROA296 (main chargen).
		// On the SEM702 variant, the ROA327 half of the chargen address
		// space is backed by RAM that the host programs via 0xD1/0xD2/0xD3.
		uint16_t offset = (linecount & 15) | (charcode << 4);
		if (BIT(attrcode, GPA0))
		{
			if (m_has_sem702)
				gfx = m_sem702_ram[offset & 0x7ff];
			else
				gfx = m_p_chargen[offset | 0x800];
		}
		else
		{
			gfx = m_p_chargen[offset];
		}
	}

	if (BIT(attrcode, LTEN))
		gfx = 0xff;

	if (BIT(attrcode, RVV))
		gfx ^= 0xff;

	// Highlight not used
	// Bits 0-6 are the 7 visible pixels (bit 7 unused in both ROA296 and ROA327 ROMs)
	bitmap.pix(y, x++) = palette[BIT(gfx, 0) ? 1 : 0];
	bitmap.pix(y, x++) = palette[BIT(gfx, 1) ? 1 : 0];
	bitmap.pix(y, x++) = palette[BIT(gfx, 2) ? 1 : 0];
	bitmap.pix(y, x++) = palette[BIT(gfx, 3) ? 1 : 0];
	bitmap.pix(y, x++) = palette[BIT(gfx, 4) ? 1 : 0];
	bitmap.pix(y, x++) = palette[BIT(gfx, 5) ? 1 : 0];
	bitmap.pix(y, x++) = palette[BIT(gfx, 6) ? 1 : 0];
}

// Baud rate generator. All inputs are 0.614MHz.
void rc702_state::clock_w(int state)
{
	m_ctc1->trg0(state);
	m_ctc1->trg1(state);
	if (m_beepcnt == 0)
		m_beep->set_state(0);
	if (m_beepcnt < 0xfe00)
		m_beepcnt--;
}

void rc702_state::hreq_w(int state)
{
	m_maincpu->set_input_line(INPUT_LINE_HALT, state ? ASSERT_LINE : CLEAR_LINE);
	m_dma->hack_w(state); // tell dma that bus has been granted
}

uint8_t rc702_state::memory_read_byte(offs_t offset)
{
	return m_maincpu->space(AS_PROGRAM).read_byte(offset);
}

void rc702_state::memory_write_byte(offs_t offset, uint8_t data)
{
	m_maincpu->space(AS_PROGRAM).write_byte(offset,data);
}

static const z80_daisy_config daisy_chain_intf[] =
{
	{ "ctc1" },
	{ "sio1" },
	{ "pio" },
	{ nullptr }
};

void rc702_state::kbd_put(u8 data)
{
	m_kbd_data = data;
	m_pio->strobe_a(0);
	m_pio->strobe_a(1);
}

uint8_t rc702_state::kbd_r()
{
	return m_kbd_data;
}

// Default RS232 settings for SIO Channel A (data/reader port).
// Must match the BIOS CONFI block (boot_confi.c): 38400 baud, 8-N-1.
// RTS flow control: BIOS deasserts RTS when ring buffer is nearly full,
// null_modem pauses transmission until RTS is reasserted.
static DEVICE_INPUT_DEFAULTS_START( rs232a_defaults )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_38400 )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_38400 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
	DEVICE_INPUT_DEFAULTS( "FLOW_CONTROL", 0x07, 0x01 )
DEVICE_INPUT_DEFAULTS_END

// Default RS232 settings for SIO Channel B (console/debug port).
// Must match the BIOS CONFI block (boot_confi.c): 38400 baud, 8-N-1.
// RTS flow control (0x01): BIOS deasserts RTS-B when its RX ring is
// near-full, so the null_modem pauses TX until the ring drains. Needed
// for reliable 4 KB-plus transfers at full line rate.
static DEVICE_INPUT_DEFAULTS_START( rs232b_defaults )
	DEVICE_INPUT_DEFAULTS( "RS232_TXBAUD", 0xff, RS232_BAUD_38400 )
	DEVICE_INPUT_DEFAULTS( "RS232_RXBAUD", 0xff, RS232_BAUD_38400 )
	DEVICE_INPUT_DEFAULTS( "RS232_DATABITS", 0xff, RS232_DATABITS_8 )
	DEVICE_INPUT_DEFAULTS( "RS232_PARITY", 0xff, RS232_PARITY_NONE )
	DEVICE_INPUT_DEFAULTS( "RS232_STOPBITS", 0xff, RS232_STOPBITS_1 )
	DEVICE_INPUT_DEFAULTS( "FLOW_CONTROL", 0x07, 0x01 )
DEVICE_INPUT_DEFAULTS_END

static void rc702_floppies(device_slot_interface &device)
{
	device.option_add("8dsdd", FLOPPY_8_DSDD);
}

static void rc702mini_floppies(device_slot_interface &device)
{
	device.option_add("525dd", FLOPPY_525_DD);
}

static void rc703_floppies(device_slot_interface &device)
{
	device.option_add("525qd", FLOPPY_525_QD);
}

void rc702_state::rc700_base(machine_config &config)
{
	/* basic machine hardware */
	Z80(config, m_maincpu, XTAL(8'000'000) / 2);
	m_maincpu->set_addrmap(AS_PROGRAM, &rc702_state::mem_map);
	m_maincpu->set_addrmap(AS_IO, &rc702_state::io_map);
	m_maincpu->set_daisy_config(daisy_chain_intf);

	CLOCK(config, "ctc_clock", 614000).signal_handler().set(FUNC(rc702_state::clock_w));

	Z80CTC(config, m_ctc1, 8_MHz_XTAL / 2);
	m_ctc1->zc_callback<0>().set("sio1", FUNC(z80sio_device::txca_w));
	m_ctc1->zc_callback<0>().append("sio1", FUNC(z80sio_device::rxca_w));
	m_ctc1->zc_callback<1>().set("sio1", FUNC(z80sio_device::rxtxcb_w));
	m_ctc1->intr_callback().set_inputline(m_maincpu, INPUT_LINE_IRQ0);

	z80sio_device& dart(Z80SIO(config, "sio1", XTAL(8'000'000) / 2));
	dart.out_int_callback().set_inputline(m_maincpu, INPUT_LINE_IRQ0);
	dart.out_txda_callback().set("rs232a", FUNC(rs232_port_device::write_txd));
	dart.out_rtsa_callback().set("rs232a", FUNC(rs232_port_device::write_rts));
	dart.out_dtra_callback().set("rs232a", FUNC(rs232_port_device::write_dtr));
	dart.out_txdb_callback().set("rs232b", FUNC(rs232_port_device::write_txd));
	dart.out_rtsb_callback().set("rs232b", FUNC(rs232_port_device::write_rts));
	dart.out_dtrb_callback().set("rs232b", FUNC(rs232_port_device::write_dtr));

	RS232_PORT(config, m_rs232a, default_rs232_devices, "null_modem");
	m_rs232a->set_option_device_input_defaults("null_modem", DEVICE_INPUT_DEFAULTS_NAME(rs232a_defaults));
	m_rs232a->rxd_handler().set("sio1", FUNC(z80sio_device::rxa_w));
	m_rs232a->cts_handler().set("sio1", FUNC(z80sio_device::ctsa_w));
	m_rs232a->dcd_handler().set("sio1", FUNC(z80sio_device::dcda_w));

	RS232_PORT(config, m_rs232b, default_rs232_devices, "null_modem");
	m_rs232b->set_option_device_input_defaults("null_modem", DEVICE_INPUT_DEFAULTS_NAME(rs232b_defaults));
	m_rs232b->rxd_handler().set("sio1", FUNC(z80sio_device::rxb_w));
	m_rs232b->cts_handler().set("sio1", FUNC(z80sio_device::ctsb_w));
	m_rs232b->dcd_handler().set("sio1", FUNC(z80sio_device::dcdb_w));

	Z80PIO(config, m_pio, 8_MHz_XTAL / 2);
	m_pio->out_int_callback().set_inputline(m_maincpu, INPUT_LINE_IRQ0);
	// PIO-A: keyboard wired direct (no slot wrapper) — Einstein topology.
	// See ravn/mame#6 for why both halves of one Z80-PIO can't both be
	// slot devices: MAME's z80pio_device is flat (no per-channel
	// device_t subdevices), and the slot mechanism has only ever been
	// validated against per-channel-subdevice chips like Z80-SIO.
	m_pio->in_pa_callback().set(FUNC(rc702_state::kbd_r));
	// PIO-B: configurable slot device.  Default empty (matches factory
	// state of the J3 expansion connector).
	m_pio->in_pb_callback().set(m_pio_b, FUNC(rc702_pio_port_device::read));
	m_pio->out_pb_callback().set(m_pio_b, FUNC(rc702_pio_port_device::write));
	m_pio->out_brdy_callback().set(m_pio_b, FUNC(rc702_pio_port_device::rdy_w));

	// PIO-B slot — out_strobe_handler wires back into the chip's strobe
	// input so cards can pulse STB.  No clock arg — the 3-arg device
	// constructor is the one that registers the card option list
	// (mirrors the EINSTEIN_USERPORT pattern in src/mame/tatung/einstein.cpp).
	RC702_PIO_PORT(config, m_pio_b);
	m_pio_b->out_strobe_handler().set(m_pio, FUNC(z80pio_device::strobe_b));

	// Direct keyboard wiring on PIO-A.  generic_keyboard pushes bytes
	// into kbd_put which latches m_kbd_data and pulses strobe_a; the
	// PIO's in_pa_callback returns m_kbd_data via kbd_r.
	generic_keyboard_device &keyboard(GENERIC_KEYBOARD(config, "keyboard"));
	keyboard.set_keyboard_callback(FUNC(rc702_state::kbd_put));

	AM9517A(config, m_dma, 8_MHz_XTAL / 2);
	m_dma->out_hreq_callback().set(FUNC(rc702_state::hreq_w));
	m_dma->out_eop_callback().set(FUNC(rc702_state::eop_w)).invert();   // real line is active low, mame has it backwards
	m_dma->in_memr_callback().set(FUNC(rc702_state::memory_read_byte));
	m_dma->out_memw_callback().set(FUNC(rc702_state::memory_write_byte));
	m_dma->out_iow_callback<2>().set("crtc", FUNC(i8275_device::dack_w));
	m_dma->out_iow_callback<3>().set("crtc", FUNC(i8275_device::dack_w));
	// ch2 DACK feeds the 74LS32 OR gate on MIC 11 (alongside the
	// 8237's TC/EOP output) -- the OR gate clocks the 74LS74 that
	// gates DRQ between ch2 and ch3 for the 8275's roll function.
	// See rc700-gensmedet/docs/dma_ch3_8275_roll_function.md.
	m_dma->out_dack_callback<2>().set(FUNC(rc702_state::dack2_w));
	// Note: out_dack_callback<1> (FDC) is wired per-variant in
	// rc702() / rc702mini() / rc703() because each
	// variant uses a different UPD765A clock + floppy geometry.
	// Upstream's 2026 reorganization moved this to rc700_base; we
	// keep it per-variant to preserve the multi-machine structure.

	TTL7474(config, m_7474);
	m_7474->output_cb().set(FUNC(rc702_state::q_w));
	m_7474->comp_output_cb().set(FUNC(rc702_state::qbar_w));

	/* video hardware */
	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER));
	screen.set_refresh_hz(50);
	// 80 chars * 7 px/char = 560 visible columns.  Previous values
	// (272*2 = 544) clipped roughly the last 2 chars on every row --
	// column 80 was off-screen in both live view and -aviwrite captures.
	// Layout file (rc702.lay) targets 608 PAR-correct cols including
	// overscan; 560 is the minimum that covers all 80 chars without
	// horizontal blanking spilling into the displayable area.
	screen.set_size(560, 200+4*8);
	screen.set_visarea(0, 559, 0, 199);
	screen.set_screen_update("crtc", FUNC(i8275_device::screen_update));

	i8275_device &crtc(I8275(config, "crtc", 11640000/7));
	crtc.set_character_width(7);
	crtc.set_display_callback(FUNC(rc702_state::display_pixels));
	crtc.irq_wr_callback().set(m_7474, FUNC(ttl7474_device::clear_w)).invert();
	crtc.irq_wr_callback().append(m_ctc1, FUNC(z80ctc_device::trg2));
	crtc.drq_wr_callback().set(FUNC(rc702_state::crtc_drq_w));

	PALETTE(config, m_palette, FUNC(rc702_state::rc702_palette), 2);

	config.set_default_layout(layout_rc702);

	/* sound hardware */
	SPEAKER(config, "mono").front_center();
	BEEP(config, m_beep, 1000).add_route(ALL_OUTPUTS, "mono", 0.50);
}

void rc702_state::rc702(machine_config &config)
{
	rc700_base(config);

	UPD765A(config, m_fdc, 8_MHz_XTAL, true, true);
	m_fdc->intrq_wr_callback().set(m_ctc1, FUNC(z80ctc_device::trg3));
	m_fdc->drq_wr_callback().set(m_dma, FUNC(am9517a_device::dreq1_w));
	m_dma->in_ior_callback<1>().set(m_fdc, FUNC(upd765a_device::dma_r));
	m_dma->out_iow_callback<1>().set(m_fdc, FUNC(upd765a_device::dma_w));
	m_dma->out_dack_callback<1>().set(FUNC(rc702_state::dack1_w));

	FLOPPY_CONNECTOR(config, "fdc:0", rc702_floppies, "8dsdd", floppy_image_device::default_mfm_floppy_formats).enable_sound(false);
	FLOPPY_CONNECTOR(config, "fdc:1", rc702_floppies, "8dsdd", floppy_image_device::default_mfm_floppy_formats).enable_sound(false);
}

void rc702_state::rc702mini(machine_config &config)
{
	rc700_base(config);

	UPD765A(config, m_fdc, 8_MHz_XTAL / 2, true, true);  // 4 MHz for 5.25" mini drives
	m_fdc->intrq_wr_callback().set(m_ctc1, FUNC(z80ctc_device::trg3));
	m_fdc->drq_wr_callback().set(m_dma, FUNC(am9517a_device::dreq1_w));
	m_dma->in_ior_callback<1>().set(m_fdc, FUNC(upd765a_device::dma_r));
	m_dma->out_iow_callback<1>().set(m_fdc, FUNC(upd765a_device::dma_w));
	m_dma->out_dack_callback<1>().set(FUNC(rc702_state::dack1_w));

	FLOPPY_CONNECTOR(config, "fdc:0", rc702mini_floppies, "525dd", floppy_image_device::default_mfm_floppy_formats).enable_sound(false);
	FLOPPY_CONNECTOR(config, "fdc:1", rc702mini_floppies, "525dd", floppy_image_device::default_mfm_floppy_formats).enable_sound(false);
}

void rc702_state::rc703(machine_config &config)
{
	rc700_base(config);

	UPD765A(config, m_fdc, 8_MHz_XTAL / 2, true, true);  // 4 MHz for 5.25" QD drives
	m_fdc->intrq_wr_callback().set(m_ctc1, FUNC(z80ctc_device::trg3));
	m_fdc->drq_wr_callback().set(m_dma, FUNC(am9517a_device::dreq1_w));
	m_dma->in_ior_callback<1>().set(m_fdc, FUNC(upd765a_device::dma_r));
	m_dma->out_iow_callback<1>().set(m_fdc, FUNC(upd765a_device::dma_w));
	m_dma->out_dack_callback<1>().set(FUNC(rc702_state::dack1_w));

	FLOPPY_CONNECTOR(config, "fdc:0", rc703_floppies, "525qd", floppy_image_device::default_mfm_floppy_formats).enable_sound(false);
	FLOPPY_CONNECTOR(config, "fdc:1", rc703_floppies, "525qd", floppy_image_device::default_mfm_floppy_formats).enable_sound(false);
	// TODO: Hard disk ports 0x60-0x67, CTC2 ports 0x44-0x47
}

// RC702 8" variant with SEM702 RAM-based chargen board fitted in IC82.
// Identical to rc702 (same FDC, floppies, etc.) except the upper 2 KB of
// the "chargen" region is RAM that the CPU programmes via ports
// 0xD1/0xD2/0xD3.  Selected at boot by display_pixels() reading from
// m_sem702_ram when GPA0 is set; m_has_sem702 distinguishes the variant
// at run time.
void rc702_state::rc702sem702(machine_config &config)
{
	rc702(config);
	m_has_sem702 = true;
}


/* ROM definition */
ROM_START( rc702 )
	// IC66 socket accepts either a 2716 (2 KB) or a 2732 (4 KB) EPROM.
	// The exact hardware mechanism for selecting the active size has not
	// yet been determined.  Region is 0x1000 with ERASEFF fill; dumps
	// smaller than 4 KB occupy the low half and the remainder is 0xff.
	ROM_REGION( 0x1000, "maincpu", ROMREGION_ERASEFF )
	ROM_SYSTEM_BIOS(0, "rc702", "RC702")
	ROMX_LOAD( "roa375.ic66", 0x0000, 0x0800, CRC(034cf9ea) SHA1(306af9fc779e3d4f51645ba04f8a99b11b5e6084), ROM_BIOS(0))
	ROM_SYSTEM_BIOS(1, "rc703", "RC703")
	ROMX_LOAD( "rob357.rom",  0x0000, 0x0800, CRC(dcf84a48) SHA1(7190d3a898bcbfa212178a4d36afc32bbbc166ef), ROM_BIOS(1))
	ROM_SYSTEM_BIOS(2, "rc700", "RC700")
	ROMX_LOAD( "rob358.rom",  0x0000, 0x0800, CRC(254aa89e) SHA1(5fb1eb8df1b853b931e670a2ff8d062c1bd8d6bc), ROM_BIOS(2))

	ROM_REGION( 0x1000, "prom1", ROMREGION_ERASEFF ) // 2716 (2KB) or 2732 (4KB), jumper-selectable
	// line program ROM (ROB388 on MIC705) - undumped prom1.ic65.
	// Optional load: drop a prom1.ic65 file into the rc702 rom path to
	// use a user-supplied image (e.g., CP/NOS resident helpers).  When
	// the file is absent, ROMREGION_ERASEFF keeps the original 0xFF fill.
	ROM_LOAD_OPTIONAL( "prom1.ic65", 0x0000, 0x1000, NO_DUMP )

	ROM_REGION( 0x1000, "chargen", 0 )
	ROM_LOAD( "roa296.rom", 0x0000, 0x0800, CRC(7d7e4548) SHA1(efb8b1ece5f9eeca948202a6396865f26134ff2f) ) // char
	ROM_LOAD( "roa327.rom", 0x0800, 0x0800, CRC(bed7ddb0) SHA1(201ae9e4ac3812577244b9c9044fadd04fb2b82f) ) // semi_gfx
ROM_END

/* RC702 8" with SEM702 RAM-based chargen in IC82.  ROA296 still occupies
 * the lower half of the chargen region; the upper half (where ROA327
 * lives on stock hardware) is replaced at run time by m_sem702_ram and
 * the static ROM contents here are never read on this variant. */
ROM_START( rc702sem702 )
	ROM_REGION( 0x1000, "maincpu", ROMREGION_ERASEFF )
	ROM_SYSTEM_BIOS(0, "rc702", "RC702")
	ROMX_LOAD( "roa375.ic66", 0x0000, 0x0800, CRC(034cf9ea) SHA1(306af9fc779e3d4f51645ba04f8a99b11b5e6084), ROM_BIOS(0))

	ROM_REGION( 0x1000, "prom1", ROMREGION_ERASEFF )
	ROM_LOAD_OPTIONAL( "prom1.ic65", 0x0000, 0x1000, NO_DUMP )

	ROM_REGION( 0x1000, "chargen", ROMREGION_ERASEFF )
	ROM_LOAD( "roa296.rom", 0x0000, 0x0800, CRC(7d7e4548) SHA1(efb8b1ece5f9eeca948202a6396865f26134ff2f) )
	// IC82 = SEM702 RAM board, not a ROA327 ROM.  Upper half of "chargen"
	// stays as 0xFF (unused) -- display_pixels reads m_sem702_ram for
	// GPA0=1 character codes on this variant.
ROM_END

} // anonymous namespace


/* Driver */

#define rom_rc702mini  rom_rc702
#define rom_rc703      rom_rc702

//    YEAR  NAME         PARENT  COMPAT  MACHINE      INPUT        CLASS        INIT        COMPANY           FULLNAME                          FLAGS
COMP( 1979, rc702,       0,      0,      rc702,       rc702_maxi,  rc702_state, empty_init, "Regnecentralen", "RC702 Piccolo (8\")",             MACHINE_SUPPORTS_SAVE )
COMP( 1979, rc702mini,   rc702,  0,      rc702mini,   rc702_mini,  rc702_state, empty_init, "Regnecentralen", "RC702 Piccolo (5.25\")",          MACHINE_SUPPORTS_SAVE )
COMP( 1982, rc703,       rc702,  0,      rc703,       rc702_mini,  rc702_state, empty_init, "Regnecentralen", "RC703 Piccolo (5.25\")",          MACHINE_SUPPORTS_SAVE )

COMP( 1979, rc702sem702, rc702,  0,      rc702sem702, rc702_maxi,  rc702_state, empty_init, "Regnecentralen", "RC702 Piccolo (8\", SEM702)",    MACHINE_SUPPORTS_SAVE )
