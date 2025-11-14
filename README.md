# Peripheral Node 🕹️

Real-time Opus audio streaming receiver for the [ESP32-LyraT V4.3](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/get-started-esp32-lyrat.html) development board. This peripheral node receives Opus-encoded audio streams over UDP multicast and plays them through the I2S audio output.

<p align="center">
<img
    alt="ESP32-LyraT V4.3 board"
    src="https://raw.githubusercontent.com/Heaven-Waves/assets/main/img/ESP32-LyraT_V4.3.png"
    width="600"
    >
</p>

## Features

- **Real-time Audio Streaming**: Receives UDP multicast audio packets with low latency
- **Opus Audio Decoding**: Hardware-accelerated Opus codec decoding (48kHz stereo)
- **RTP Protocol Support**: Parses RTP headers to extract audio payload
- **Robust WiFi Handling**: Automatic reconnection and event-driven network management
- **Dual-Core Utilization**: UDP receiver on core 1, I2S playback on core 0
- **Memory Optimized**: Uses external PSRAM for extended memory allocation
- **Configurable Parameters**: WiFi, network, and audio settings via menuconfig

## Architecture

```
UDP Multicast (224.1.1.1:5005)
    ↓
RTP Packet Reception
    ↓
RTP Header Parsing
    ↓
Opus Payload Extraction
    ↓
Opus Decoder (48kHz Stereo)
    ↓
PCM Audio Buffer
    ↓
Ring Buffer (8KB)
    ↓
I2S Writer Element
    ↓
ES8388 Audio Codec
    ↓
Speaker/Headphone Output
```

## Hardware Requirements

- **Board**: ESP32-LyraT V4.3
- **Microcontroller**: ESP32 (dual-core Xtensa)
- **Audio Codec**: ES8388
- **Memory**: 520KB SRAM + 4MB PSRAM
- **Flash**: 8MB
- **Network**: WiFi 802.11 b/g/n

## Software Requirements

- **ESP-IDF**: v5.3.1 (Espressif IoT Development Framework)
- **ESP-ADF**: v2.7 (Audio Development Framework)
- **Build Tools**: CMake, Make
- **Compiler**: xtensa-esp32-elf-gcc (esp-13.2.0)

## Getting Started

### 1. Install Prerequisites

Follow the [ESP-IDF installation guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html) to set up ESP-IDF v5.3.1.

Install ESP-ADF v2.7:
```bash
cd ~/esp
git clone --recursive https://github.com/espressif/esp-adf.git
cd esp-adf
git checkout v2.7
```

### 2. Clone Repository

```bash
git clone <repository-url>
cd peripheral-node
```

### 3. Configure the Project

```bash
idf.py menuconfig
```

Navigate to configure:

**WiFi Configuration**:
- SSID: Your WiFi network name
- Password: Your WiFi password

**Audio Streaming Configuration**:
- Multicast IP: Default `224.1.1.1`
- UDP Port: Default `5005`
- Sample Rate: Default `48000` Hz
- Bits per Sample: Default `16`
- Channels: Default `2` (stereo)

**Opus Configuration**:
- Frame Size: Default `960` samples (20ms at 48kHz)
- Max Packet Size: Default `2048` bytes
- Jitter Buffer Size: Default `2048` bytes

### 4. Build and Flash

```bash
# Build the project
idf.py build

# Flash to the ESP32-LyraT board
idf.py -p /dev/ttyUSB0 flash

# Monitor output (optional)
idf.py -p /dev/ttyUSB0 monitor
```

Press `Ctrl+]` to exit the monitor.

### 5. Flash and Monitor in One Command

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Configuration Options

All configuration options are accessible via `idf.py menuconfig`:

### WiFi Settings
- `WIFI_SSID`: WiFi network name (default: "YourWiFiSSID")
- `WIFI_PASSWORD`: WiFi password (default: "YourWiFiPassword")

### Network Settings
- `MULTICAST_IP`: Multicast group address (default: "224.1.1.1")
- `UDP_PORT`: UDP port number (default: 5005)

### Audio Settings
- `AUDIO_SAMPLE_RATE`: Sample rate in Hz (default: 48000)
- `BITS_PER_SAMPLE`: Bits per sample (default: 16)
- `AUDIO_CHANNELS`: Number of channels 1-2 (default: 2)

### Opus Decoder Settings
- `OPUS_FRAME_SIZE`: Frame size in samples (default: 960)
- `MAX_PACKET_SIZE`: Maximum packet size (default: 2048)
- `JITTER_BUFFER_SIZE`: Jitter buffer size (default: 2048)

## Project Structure

```
peripheral-node/
├── main/
│   ├── main.c                    # Main application logic
│   ├── Kconfig.projbuild         # Configuration menu definitions
│   └── idf_component.yml         # Component dependencies
├── components/
│   ├── logs/                     # Custom logging component
│   │   ├── logs.h
│   │   └── logs.c
│   └── pipeline/                 # Audio pipeline wrapper component
│       ├── pipeline.h
│       └── pipeline.c
├── partitions.csv                # Flash partition table
├── sdkconfig.defaults            # Default build configuration
├── CMakeLists.txt                # Root CMake file
├── dependencies.lock             # Locked component versions
└── README.md                     # This file
```

## Key Components

### Main Application (main/main.c)

The main application orchestrates:
- WiFi connection and management
- UDP multicast socket setup
- RTP packet reception and parsing
- Opus audio decoding
- I2S audio output via ring buffer

**Key Functions**:
- `wifi_init_sta()`: Initialize WiFi in station mode
- `setup_udp_multicast()`: Create UDP socket and join multicast group
- `get_rtp_payload()`: Parse RTP headers and extract Opus payload
- `udp_receiver_task()`: Main receiver task (runs on core 1)
- `setup_audio_pipeline()`: Initialize I2S audio output with ring buffer

### Custom Components

**logs**: Simplified logging interface
- `logi()`, `loge()`, `logw()` functions
- Consistent "PERIPHERAL_NODE" tag

**pipeline**: Audio pipeline wrapper (ESP-ADF)
- Simplified API for pipeline management
- Functions for init, deinit, register, link, run, stop

## Hardware Configuration

### I2S Configuration
- **Port**: I2S_NUM_0
- **Sample Rate**: 48000 Hz (configurable)
- **Channels**: 2 (Stereo, configurable)
- **Bits per Sample**: 16-bit (configurable)
- **Mode**: Slave mode, normal I2S format

### Audio Codec (ES8388)
- **Input**: LINE1
- **Output**: All outputs enabled
- **Mode**: Decode only (playback)
- **Volume**: 100%

### Memory Configuration
- **PSRAM**: Enabled with malloc support
- **Ring Buffer**: 8KB for I2S input
- **Static Buffers**: PCM (8KB), UDP receive (512B)
- **Stack**: Allocated in external memory

### Network Configuration
- **WiFi Buffers**: 10 static, 32 dynamic RX/TX
- **LWIP Receive Buffer**: 8KB
- **Multicast**: Enabled

## Performance

- **CPU Frequency**: 240 MHz
- **FreeRTOS Tick Rate**: 1000 Hz
- **Task Watchdog**: 10 seconds
- **Latency**: Optimized for real-time streaming
- **Memory Usage**: Monitored with heap tracking

## Monitoring and Debugging

The application provides detailed logging:
- Packet reception statistics
- Ring buffer fill levels
- Memory usage (heap monitoring)
- Packet content inspection (first 8 bytes)
- I2S element state tracking

Set log level in menuconfig:
```
Component config → Log output → Default log verbosity
```

## Troubleshooting

### WiFi Connection Issues
- Verify SSID and password in menuconfig
- Check WiFi signal strength
- Monitor logs for connection events

### No Audio Output
- Verify multicast IP and port match sender
- Check audio codec initialization in logs
- Ensure ring buffer is receiving data
- Verify I2S configuration matches sender format

### Memory Issues
- Monitor heap usage in logs
- Adjust ring buffer size if needed
- Check PSRAM is properly initialized

### Packet Loss
- Increase WiFi buffer sizes in sdkconfig.defaults
- Adjust jitter buffer size
- Check network congestion

## Development

### VSCode Setup

The project includes VSCode configuration for ESP-IDF extension:
- C/C++ IntelliSense configured
- Build and flash tasks available
- Serial monitor integration

### Adding Custom Components

1. Create component directory under `components/`
2. Add `CMakeLists.txt` with `idf_component_register()`
3. Implement your component logic
4. Include in main application

### Modifying Audio Pipeline

The audio pipeline can be customized in `setup_audio_pipeline()` in main/main.c:
- Change sample rate/channels
- Add audio effects
- Modify ring buffer size
- Add additional audio elements

## Future Enhancements

- Forward Error Correction (FEC) support
- Multiple codec support (AAC, MP3)
- Adaptive bitrate streaming
- Web-based configuration interface
- OTA (Over-The-Air) firmware updates
- Multi-room synchronization

## License

Part of a thesis project on distributed audio systems.

## References

- [ESP-IDF Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/)
- [ESP-ADF Documentation](https://docs.espressif.com/projects/esp-adf/en/latest/)
- [ESP32-LyraT V4.3 Guide](https://docs.espressif.com/projects/esp-adf/en/latest/design-guide/dev-boards/get-started-esp32-lyrat.html)
- [Opus Codec](https://opus-codec.org/)
- [RTP Protocol (RFC 3550)](https://datatracker.ietf.org/doc/html/rfc3550)
