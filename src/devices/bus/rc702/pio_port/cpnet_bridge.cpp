// license: BSD-3-Clause
// copyright-holders: ravn
/***************************************************************************

    RC702 CP/NET host bridge — Z80 PIO port slot card

    Bytes flow between the Z80 PIO chip and an external client (the
    Pi/Pico bridge in production, or a Python test harness during
    bring-up) via a bitbanger image device.  Bitbanger handles the
    socket plumbing (OSD posix_osd_socket: non-blocking, polled
    inline by the emu thread).

    Direction tracking is purely protocol-driven: the chip's own mode
    routing decides which callback (read or write) fires.  No
    port-0x13 sniff, no chip-mode observation.

    See cpnet_bridge.h for the file-history note on why this device
    moved from a private listener thread + raw POSIX sockets to the
    bitbanger pattern (TL;DR: ~50x per-frame latency win in MAME).

***************************************************************************/

#include "emu.h"
#include "cpnet_bridge.h"


//**************************************************************************
//  DEVICE DEFINITION
//**************************************************************************

DEFINE_DEVICE_TYPE(RC702_PIO_CPNET_BRIDGE, rc702_pio_cpnet_bridge_device, "rc702_pio_cpnet_bridge", "RC702 CP/NET Host Bridge")


//**************************************************************************
//  IMPLEMENTATION
//**************************************************************************

rc702_pio_cpnet_bridge_device::rc702_pio_cpnet_bridge_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock) :
	device_t(mconfig, RC702_PIO_CPNET_BRIDGE, tag, owner, clock),
	device_rc702_pio_port_interface(mconfig, *this),
	m_stream(*this, "stream"),
	m_poll_timer(nullptr),
	m_input_count(0),
	m_input_index(0),
	m_brdy_high(true)  // PIO defaults BRDY high after reset
{
}

void rc702_pio_cpnet_bridge_device::device_add_mconfig(machine_config &config)
{
	BITBANGER(config, m_stream, 0);
}

void rc702_pio_cpnet_bridge_device::device_start()
{
	// Poll the bitbanger for inbound bytes + drive STB to the chip
	// when m_input_buffer has data and BRDY is high.  1 ms cadence
	// is well below CP/NET frame round-trip latency yet well above
	// per-byte chip turnaround (~40 µs at 4 MHz Z80).
	m_poll_timer = timer_alloc(FUNC(rc702_pio_cpnet_bridge_device::poll_tick), this);
	m_poll_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));

	save_item(NAME(m_input_buffer));
	save_item(NAME(m_input_count));
	save_item(NAME(m_input_index));
	save_item(NAME(m_brdy_high));
}


//**************************************************************************
//  CHIP-SIDE CALLBACKS (run on emulation thread)
//
//  STB timing — Mode 1 input flow:
//
//    1. Bridge places data on PB lines (= return value from read()).
//    2. Bridge pulses STB low-then-high.
//    3. PIO latches data on STB rising edge, asserts INT, drops BRDY.
//    4. Z80 ISR runs, IN A,(0x11) returns the latched byte, INT clears.
//    5. PIO raises BRDY -> rdy_w(1) here -> we strobe the next byte.
//
//**************************************************************************

uint8_t rc702_pio_cpnet_bridge_device::read()
{
	if (m_input_index < m_input_count)
		return m_input_buffer[m_input_index++];

	// Empty: try a lazy refill so single-byte reads after long quiet
	// periods don't have to wait for the next poll_tick.  Bitbanger's
	// input() is non-blocking via the OSD layer.
	m_input_count = m_stream->input(m_input_buffer, sizeof(m_input_buffer));
	m_input_index = 0;
	if (m_input_index < m_input_count)
		return m_input_buffer[m_input_index++];

	// Truly empty FIFO sentinel — matches the prior cpnet_bridge
	// contract that callers (cpnos-rom transport_pio.c) rely on for
	// the first-byte busy-wait in pio_recv_msg.
	return 0xff;
}

void rc702_pio_cpnet_bridge_device::write(uint8_t data)
{
	m_stream->output(data);

	// Pulse STB so the chip releases BRDY (Mode 0 output ack
	// semantic).  Output direction is symmetric — chip wrote, we
	// ack to release.
	m_slot->strobe_w(0);
	m_slot->strobe_w(1);
}

void rc702_pio_cpnet_bridge_device::rdy_w(int state)
{
	bool const was_high = m_brdy_high;
	m_brdy_high = (state != 0);

	// Rising edge: strobe only when there's a real byte to deliver.
	// On real Pi/Pico hardware this corresponds to "only pulse STB
	// when the TCP queue has something."  Empty buffer + no strobe
	// = no chip interrupt = Z80 ISR doesn't fire = snios's recv
	// queue stays empty and the recv loop times out cleanly.
	//
	// Earlier "always strobe" variant generated empty-strobes that
	// latched 0xff into the chip's m_input as a "no byte" sentinel,
	// which conflated a real 0xff data byte from mpm-net2 with
	// "buffer empty" in the polled snios path — see
	// rc700-gensmedet/tasks/session34-direct-pio-stall-rootcause.md.
	// The IRQ-driven snios path on the rc700-gensmedet
	// pio-mpm-irq-fix branch removes the polling and so this gate
	// is the right one again.
	if (m_brdy_high && !was_high && m_input_index < m_input_count)
	{
		m_slot->strobe_w(0);
		m_slot->strobe_w(1);
	}
}

TIMER_CALLBACK_MEMBER(rc702_pio_cpnet_bridge_device::poll_tick)
{
	// Refill the input buffer if drained.  Bitbanger's input() is
	// non-blocking: returns whatever is currently available (0 if
	// the host hasn't sent anything since the last poll).
	if (m_input_index >= m_input_count)
	{
		m_input_count = m_stream->input(m_input_buffer, sizeof(m_input_buffer));
		m_input_index = 0;
	}

	// Strobe whenever there's a real byte to deliver.  The
	// m_brdy_high gate isn't reliable: MAME's z80pio.cpp does not
	// auto-raise BRDY when the chip enters MODE_INPUT (real chip
	// does, per Zilog datasheet), so m_rdy can be stuck false
	// after a previous mode-flip even though the chip is happy to
	// accept a strobe.  In MODE_INPUT, chip strobe handler is
	// ungated and processes correctly.  In MODE_OUTPUT (the
	// transient SEND phase), strobe(0) is a no-op and strobe(1) is
	// gated on m_rdy inside the chip — strobing here is harmless.
	// IE is held off during OUTPUT by transport_pio.c so no IRQ
	// fires during a SEND even if the chip's m_ip flag gets set.
	if (m_input_index < m_input_count)
	{
		m_slot->strobe_w(0);
		m_slot->strobe_w(1);
	}
}
