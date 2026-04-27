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

	// Rising edge: always strobe (don't gate on buffer non-empty).
	// When buffer is empty, read() returns 0xff and the chip latches
	// 0xff into m_input — Z80's busy-poll loop treats 0xff as "no
	// byte yet" and keeps looking.  If we DON'T strobe on empty,
	// m_input keeps the previous real byte, and Z80's next IN
	// returns the same byte again, producing silent duplicate reads
	// in snios's NETIN/MSGIN — which is exactly the OPEN-response
	// failure mode in the snios-on-PIO experiment (LOGIN works
	// because its 1-byte payload has no chance for duplicates;
	// OPEN's 37-byte payload runs out of bridge buffer mid-MSGIN
	// and the trailing INs all return the same stale byte).
	if (m_brdy_high && !was_high)
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

	// If we have bytes ready and the chip is ready to accept (BRDY
	// high), strobe to start the next latch.  read() will hand the
	// byte to the chip on the corresponding IN.
	if (m_input_index < m_input_count && m_brdy_high)
	{
		m_slot->strobe_w(0);
		m_slot->strobe_w(1);
	}
}
