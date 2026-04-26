// license: BSD-3-Clause
// copyright-holders: ravn
/***************************************************************************

    RC702 CP/NET host bridge — Z80 PIO port slot card

    POSIX-socket-based TCP listener forwards CP/NET frame bytes between
    the Z80 PIO port and an external client (the Pi/Pico bridge in
    production, or a Python test harness during MAME bring-up).

    Design notes (see rc700-gensmedet/docs/cpnet_fast_link.md):
    - Direction tracking is purely protocol-driven: the chip's own mode
      routing decides which callback (read or write) fires.  No port-0x13
      sniff, no chip-mode observation.
    - Two unidirectional FIFOs: m_host_to_z80 drained by read(),
      m_z80_to_host filled by write().
    - Single listener thread handles both socket directions via select().
    - Single client at a time; new connection drops any previous client.

    POSIX-only (Mac/Linux).  MAME-on-Windows builds will skip this device
    -- not a target for the current Pi+Mac CP/NET workflow.

***************************************************************************/

#include "emu.h"
#include "cpnet_bridge.h"

#include <cerrno>
#include <cstring>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif


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
	m_port(default_port),
	m_listen_fd(-1),
	m_client_fd(-1),
	m_shutdown(false),
	m_brdy_high(true)  // PIO defaults BRDY high after reset
{
}

void rc702_pio_cpnet_bridge_device::device_start()
{
#if defined(_WIN32)
	logerror("cpnet_bridge: not supported on Windows builds; device disabled\n");
#else
	int fd = ::socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
	{
		logerror("cpnet_bridge: socket() failed: %s\n", std::strerror(errno));
		return;
	}

	int one = 1;
	::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

	struct sockaddr_in addr {};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(uint16_t(m_port));

	if (::bind(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) < 0)
	{
		logerror("cpnet_bridge: bind(127.0.0.1:%d) failed: %s\n", m_port, std::strerror(errno));
		::close(fd);
		return;
	}

	if (::listen(fd, 1) < 0)
	{
		logerror("cpnet_bridge: listen() failed: %s\n", std::strerror(errno));
		::close(fd);
		return;
	}

	m_listen_fd = fd;
	m_listener_thread = std::thread(&rc702_pio_cpnet_bridge_device::listener_thread_main, this);
	logerror("cpnet_bridge: listening on 127.0.0.1:%d\n", m_port);
#endif

	// Polling timer on the emu thread.  Periodically checks the
	// host->Z80 FIFO and pulses STB if the chip is ready.  Polling
	// rather than cross-thread signalling because synchronize() does
	// not appear to dispatch reliably from a non-emu thread for our
	// device init order.  1 ms cadence: well below CP/NET frame
	// latency (~10ms+), well above per-byte latch turnaround (~40 µs
	// at 4 MHz Z80).
	m_poll_timer = timer_alloc(FUNC(rc702_pio_cpnet_bridge_device::poll_tick), this);
	m_poll_timer->adjust(attotime::from_msec(1), 0, attotime::from_msec(1));
}

void rc702_pio_cpnet_bridge_device::device_stop()
{
#if !defined(_WIN32)
	m_shutdown = true;

	int lfd = m_listen_fd.exchange(-1);
	if (lfd >= 0)
		::close(lfd);

	close_client();

	if (m_listener_thread.joinable())
		m_listener_thread.join();
#endif
}

void rc702_pio_cpnet_bridge_device::close_client()
{
#if !defined(_WIN32)
	int cfd = m_client_fd.exchange(-1);
	if (cfd >= 0)
		::close(cfd);
#endif
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
//  Earlier code pulsed STB inside read() — i.e. AFTER the chip had
//  already pulled data — which is backwards: the rising edge that
//  drives latching never fired, so the chip never asserted INT and
//  the Z80 ISR never ran.  Bytes piled up in m_host_to_z80 forever.
//
//  schedule_strobe() is the cross-thread entry point: callable from
//  listener_thread_main(); marshals to the emu thread via
//  machine().scheduler().synchronize().
//**************************************************************************

uint8_t rc702_pio_cpnet_bridge_device::read()
{
	std::lock_guard<std::mutex> lk(m_fifo_mtx);
	if (m_host_to_z80.empty())
		return 0xff;
	uint8_t data = m_host_to_z80.front();
	m_host_to_z80.pop_front();
	return data;
}

void rc702_pio_cpnet_bridge_device::write(uint8_t data)
{
	{
		std::lock_guard<std::mutex> lk(m_fifo_mtx);
		m_z80_to_host.push_back(data);
	}
	// Pulse STB so chip releases BRDY (Mode 0 output ack semantic).
	// Output direction is symmetric — chip wrote, we ack to release.
	m_slot->strobe_w(0);
	m_slot->strobe_w(1);
}

void rc702_pio_cpnet_bridge_device::rdy_w(int state)
{
	// Track BRDY so try_strobe_synced knows when the chip is ready
	// to accept the next latch.  When BRDY rises with bytes pending,
	// strobe immediately.
	bool was_high = m_brdy_high;
	m_brdy_high = (state != 0);
	logerror("cpnet_bridge: rdy_w(%d) was_high=%d\n", state, was_high);
	if (m_brdy_high && !was_high)
	{
		bool have;
		{
			std::lock_guard<std::mutex> lk(m_fifo_mtx);
			have = !m_host_to_z80.empty();
		}
		if (have)
		{
			logerror("cpnet_bridge: rdy_w rising edge with data, strobing\n");
			m_slot->strobe_w(0);
			m_slot->strobe_w(1);
		}
	}
}

TIMER_CALLBACK_MEMBER(rc702_pio_cpnet_bridge_device::poll_tick)
{
	bool have;
	{
		std::lock_guard<std::mutex> lk(m_fifo_mtx);
		have = !m_host_to_z80.empty();
	}
	if (have && m_brdy_high)
	{
		m_slot->strobe_w(0);
		m_slot->strobe_w(1);
	}
}


//**************************************************************************
//  LISTENER THREAD
//**************************************************************************

void rc702_pio_cpnet_bridge_device::listener_thread_main()
{
#if !defined(_WIN32)
	while (!m_shutdown)
	{
		int lfd = m_listen_fd.load();
		if (lfd < 0)
			break;

		struct sockaddr_in client_addr {};
		socklen_t client_len = sizeof(client_addr);
		int cfd = ::accept(lfd, reinterpret_cast<struct sockaddr *>(&client_addr), &client_len);
		if (cfd < 0)
		{
			if (m_shutdown)
				break;
			continue;
		}

		// New connection — drop any previous client.
		close_client();
		m_client_fd = cfd;

		// Service this client until disconnect or shutdown.
		while (!m_shutdown)
		{
			fd_set rfds, wfds;
			FD_ZERO(&rfds);
			FD_ZERO(&wfds);
			FD_SET(cfd, &rfds);

			bool have_outbound;
			{
				std::lock_guard<std::mutex> lk(m_fifo_mtx);
				have_outbound = !m_z80_to_host.empty();
			}
			if (have_outbound)
				FD_SET(cfd, &wfds);

			struct timeval tv {};
			tv.tv_sec = 0;
			tv.tv_usec = 50 * 1000;  // 50ms poll

			int n = ::select(cfd + 1, &rfds, &wfds, nullptr, &tv);
			if (n < 0)
			{
				if (errno == EINTR)
					continue;
				break;
			}

			if (FD_ISSET(cfd, &rfds))
			{
				uint8_t buf[256];
				ssize_t r = ::read(cfd, buf, sizeof(buf));
				if (r <= 0)
					break;  // disconnect or error

				bool was_empty;
				{
					std::lock_guard<std::mutex> lk(m_fifo_mtx);
					was_empty = m_host_to_z80.empty();
					for (ssize_t i = 0; i < r; ++i)
						m_host_to_z80.push_back(buf[i]);
				}
				// Wake the emu thread so it can latch the first byte
				// via STB.  Subsequent bytes get strobed from rdy_w()
				// as the chip raises BRDY between latches.  Only
				// schedule on the empty->non-empty transition so we
				// don't spam the scheduler mid-burst.
				logerror("cpnet_bridge: listener got %zd bytes was_empty=%d\n",
						r, was_empty);
				// poll_tick on the emu thread will pick the new bytes
				// up on its next 1 ms tick — no cross-thread signalling
				// from here.
			}

			if (have_outbound && FD_ISSET(cfd, &wfds))
			{
				uint8_t buf[256];
				size_t fill = 0;
				{
					std::lock_guard<std::mutex> lk(m_fifo_mtx);
					while (fill < sizeof(buf) && !m_z80_to_host.empty())
					{
						buf[fill++] = m_z80_to_host.front();
						m_z80_to_host.pop_front();
					}
				}
				if (fill > 0)
				{
					ssize_t w = ::write(cfd, buf, fill);
					if (w < 0)
						break;
					// If short write, push remainder back to front of queue.
					if (size_t(w) < fill)
					{
						std::lock_guard<std::mutex> lk(m_fifo_mtx);
						for (size_t i = fill; i > size_t(w); --i)
							m_z80_to_host.push_front(buf[i - 1]);
					}
				}
			}
		}

		close_client();
	}
#endif
}
