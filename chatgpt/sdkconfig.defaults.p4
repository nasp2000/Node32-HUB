# sdkconfig.defaults.p4 — ESP32-P4-specific IDF configuration overrides.
#
# Referenced from platformio.ini [env:esp32p4] via board_build.sdkconfig_path.
# Only settings that differ from IDF defaults are listed here.

# ---------------------------------------------------------------------------
# USB Hub support — required for CH334 HUB on ports 2-4 (P4=DEVICE mode).
# Without this, the IDF USB Host Library cannot enumerate or power
# downstream ports of an external USB hub.
# ---------------------------------------------------------------------------
CONFIG_USB_HOST_ENABLE_HUBS=y

# ---------------------------------------------------------------------------
# TinyUSB — host-mode CDC streaming optimisation.
# Only relevant when ENABLE_USB_HOST=1 with TinyUSB backend.
# ---------------------------------------------------------------------------
CONFIG_TINYUSB_ENABLED=y
# Maximum size (bytes) for a single control transfer.
# IDF default is 64; 256 covers most CDC class descriptors (CP210x, CH343).
CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256
# DMA buffer split: balanced for CDC bidirectional traffic.
CONFIG_USB_HOST_HW_BUFFER_BIAS_BALANCED=y
# CDC host RX FIFO buffer: 512 bytes (TinyUSB default 64).
# Larger FIFO absorbs USB frames during GRBL streaming bursts, preventing
# data loss when the application cannot read fast enough.
CONFIG_TINYUSB_CDC_RX_BUFSIZE=512
# CDC host TX FIFO buffer: 512 bytes (TinyUSB default 64).
# Matches RX for symmetric throughput; allows batching multiple GRBL lines
# before flushing to USB hardware.
CONFIG_TINYUSB_CDC_TX_BUFSIZE=512

# ---------------------------------------------------------------------------
# lwIP — tuned for RMII 100 Mbps Ethernet on P4 with 32 MB PSRAM available.
# IDF defaults target memory-constrained devices; P4 can afford larger buffers.
# ---------------------------------------------------------------------------
# TCP receive window: 32 KB (IDF default 5744 bytes).
# Larger window = fewer ACKs needed, better throughput for SD upload / web.
CONFIG_LWIP_TCP_WND_DEFAULT=32768
# TCP send buffer: 32 KB (IDF default 5744 bytes).
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=32768
# Maximum TCP segments in the retransmit queue per connection.
CONFIG_LWIP_TCP_QUEUE_OOSEQ=1
# Number of simultaneously open TCP sockets (web + telnet + SMTP + monitor).
# IDF default is 10; 16 is safe with PSRAM available.
CONFIG_LWIP_MAX_SOCKETS=16
# PBUF pool count: each PBUF is 1.5 KB; 32 = 48 KB from PSRAM via lwIP PBUF_POOL_SIZE.
CONFIG_LWIP_PBUF_POOL_SIZE=32

# ---------------------------------------------------------------------------
# FreeRTOS tick rate: 1000 Hz gives 1 ms resolution for timeouts and delays.
# IDF default on P4 should already be 1000; explicit here to be safe.
# ---------------------------------------------------------------------------
CONFIG_FREERTOS_HZ=1000
