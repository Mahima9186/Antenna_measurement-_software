#include <windows.h>
#include <iostream>
#include <string>
#include <iomanip>
#include <cstdint>

uint16_t calculateCRC(const uint8_t* buf, int len) {
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++) {
        crc ^= (uint16_t)buf[pos];
        for (int i = 8; i != 0; i--) {
            if ((crc & 0x0001) != 0) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

bool testModbusConnection(HANDLE hSerial, uint8_t slaveId, uint16_t regAddrZeroBased) {
    uint8_t frame[8];
    frame[0] = slaveId;
    frame[1] = 0x03; // Read Holding Registers
    frame[2] = (regAddrZeroBased >> 8) & 0xFF;
    frame[3] = regAddrZeroBased & 0xFF;
    frame[4] = 0x00;
    frame[5] = 0x01; // Read 1 register

    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    if (!WriteFile(hSerial, frame, 8, &bytesWritten, NULL) || bytesWritten != 8) {
        return false;
    }

    // Expected response size: 7 bytes (1 slave, 1 fn, 1 count, 2 value, 2 crc)
    uint8_t response[7] = {0};
    DWORD bytesRead = 0;

    if (!ReadFile(hSerial, response, 7, &bytesRead, NULL)) {
        return false;
    }

    if (bytesRead != 7) {
        return false;
    }

    uint16_t respCrc = calculateCRC(response, 5);
    uint16_t respCrcReceived = response[5] | (response[6] << 8);
    if (respCrc != respCrcReceived) {
        return false;
    }

    return true;
}

void runScanner(const std::string& portName) {
    std::string formattedPort = "\\\\.\\" + portName;
    DWORD baudRates[] = { 9600, 19200, 38400, 115200 };
    int numBauds = sizeof(baudRates) / sizeof(baudRates[0]);

    // Test different Parity and Stop Bits combinations
    struct SerialConfig {
        BYTE parity;
        BYTE stopBits;
        std::string name;
    } configs[] = {
        { EVENPARITY, ONESTOPBIT, "Even Parity, 1 Stop Bit" },
        { NOPARITY, TWOSTOPBITS, "No Parity, 2 Stop Bits" },
        { NOPARITY, ONESTOPBIT, "No Parity, 1 Stop Bit (non-standard but common)" },
        { ODDPARITY, ONESTOPBIT, "Odd Parity, 1 Stop Bit" }
    };
    int numConfigs = sizeof(configs) / sizeof(configs[0]);

    std::cout << "[INFO] Starting Comprehensive Modbus Scan on " << portName << "..." << std::endl;
    std::cout << "------------------------------------------------------------" << std::endl;

    for (int b = 0; b < numBauds; b++) {
        DWORD baud = baudRates[b];

        for (int c = 0; c < numConfigs; c++) {
            SerialConfig cfg = configs[c];
            std::cout << "[SCAN] Testing: " << baud << " Baud, " << cfg.name << "..." << std::endl;

            HANDLE hSerial = CreateFileA(
                formattedPort.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                NULL,
                OPEN_EXISTING,
                0,
                NULL
            );

            if (hSerial == INVALID_HANDLE_VALUE) {
                std::cerr << "  Failed to open port " << portName << std::endl;
                continue;
            }

            DCB dcbSerialParams = {0};
            dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
            if (GetCommState(hSerial, &dcbSerialParams)) {
                dcbSerialParams.BaudRate = baud;
                dcbSerialParams.ByteSize = 8;
                dcbSerialParams.StopBits = cfg.stopBits;
                dcbSerialParams.Parity = cfg.parity;
                dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
                dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;
                SetCommState(hSerial, &dcbSerialParams);
            }

            // Tight timeouts for fast scanning
            COMMTIMEOUTS timeouts = {0};
            timeouts.ReadIntervalTimeout = 15;
            timeouts.ReadTotalTimeoutConstant = 100; // 100ms total read timeout per attempt
            timeouts.ReadTotalTimeoutMultiplier = 2;
            timeouts.WriteTotalTimeoutConstant = 15;
            timeouts.WriteTotalTimeoutMultiplier = 2;
            SetCommTimeouts(hSerial, &timeouts);

            // Scan Slave IDs 1 to 8
            for (uint8_t id = 1; id <= 8; id++) {
                // Test register addresses: 0 (AZSTBITS), 2 (AZCUR)
                uint16_t testAddresses[] = { 0, 2 };
                for (int a = 0; a < 2; a++) {
                    uint16_t addr = testAddresses[a];
                    if (testModbusConnection(hSerial, id, addr)) {
                        std::cout << "\n=============================================" << std::endl;
                        std::cout << "  FOUND DEVICE!" << std::endl;
                        std::cout << "  Baud Rate: " << baud << std::endl;
                        std::cout << "  Parity:    " << cfg.name << std::endl;
                        std::cout << "  Slave ID:  " << (int)id << std::endl;
                        std::cout << "  Address:   " << addr << " (Register " << (addr + 1) << ")" << std::endl;
                        std::cout << "=============================================" << std::endl;
                        CloseHandle(hSerial);
                        return;
                    }
                }
            }

            CloseHandle(hSerial);
        }
    }

    std::cout << "\n[SCAN COMPLETED] Scan finished. No Modbus devices responded." << std::endl;
    std::cout << "Recommendations:" << std::endl;
    std::cout << " 1. Make sure the TAP-3001 positioner is powered ON." << std::endl;
    std::cout << " 2. Verify the RS-232/RS-485 serial cable connection." << std::endl;
    std::cout << " 3. Verify that Tx/Rx wires are not swapped on the serial port adapter." << std::endl;
}

int main() {
    std::string port = "COM3";
    runScanner(port);
    return 0;
}
