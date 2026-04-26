// license: BSD-3-Clause
// copyright-holders: ravn
/***************************************************************************

    RC702 CP/NET host bridge — Z80 PIO port slot card

    Implements the host side of the CP/NET fast-link transport (see
    rc700-gensmedet/docs/cpnet_fast_link.md, "Option P").  Plug into
    PIO-B (J3 in real hardware) via the slot system:

        mame rc702 -piob cpnet_bridge

    The card exposes a TCP listener (default localhost:4003) and forwards
    bytes between the socket and the Z80 PIO.  Direction tracking is
    purely protocol-driven on the socket side (CP/NET SCB length fields);
    on the chip side, the Z80-PIO's own mode routing decides whether
    `read()` (chip pulls a byte from the bridge in Mode 1) or `write()`
    (chip pushes a byte to the bridge in Mode 0) gets called.  The bridge
    maintains two unidirectional FIFOs to match.

    No chip-mode tracking, no port-0x13 sniff — see the design doc for
    why mirroring real hardware behaviour is enough.

***************************************************************************/

#ifndef MAME_BUS_RC702_PIO_PORT_CPNET_BRIDGE_H
#define MAME_BUS_RC702_PIO_PORT_CPNET_BRIDGE_H

#pragma once

#include "pio_port.h"

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

class rc702_pio_cpnet_bridge_device :
	public device_t,
	public device_rc702_pio_port_interface
{
public:
	rc702_pio_cpnet_bridge_device(const machine_config &mconfig, const char *tag, device_t *owner, uint32_t clock = 0);

	// device_rc702_pio_port_interface
	virtual uint8_t read() override;
	virtual void write(uint8_t data) override;
	virtual void rdy_w(int state) override;

protected:
	virtual void device_start() override ATTR_COLD;
	virtual void device_stop() override ATTR_COLD;

private:
	// listener thread
	void listener_thread_main();
	void close_client();

	// emu-thread strobe scheduling — see cpnet_bridge.cpp comment
	// "STB timing — Mode 1 input flow"
	TIMER_CALLBACK_MEMBER(poll_tick);
	emu_timer *m_poll_timer;

	// configurable: TCP port the listener binds (default 4003)
	static constexpr int default_port = 4003;
	int m_port;

	// listener / client sockets (POSIX fd, -1 = invalid)
	std::atomic<int> m_listen_fd;
	std::atomic<int> m_client_fd;

	// listener thread
	std::thread m_listener_thread;
	std::atomic<bool> m_shutdown;

	// FIFOs between socket I/O and chip-side callbacks
	std::mutex m_fifo_mtx;
	std::deque<uint8_t> m_host_to_z80;  // drained by read()
	std::deque<uint8_t> m_z80_to_host;  // filled by write()

	// chip-side BRDY state.  Mode 1 input: BRDY high == chip ready to
	// accept a new latch; we only pulse STB when (a) FIFO non-empty and
	// (b) m_brdy_high.  Updated from rdy_w() on the emu thread.
	bool m_brdy_high;
};

DECLARE_DEVICE_TYPE(RC702_PIO_CPNET_BRIDGE, rc702_pio_cpnet_bridge_device)

#endif // MAME_BUS_RC702_PIO_PORT_CPNET_BRIDGE_H
