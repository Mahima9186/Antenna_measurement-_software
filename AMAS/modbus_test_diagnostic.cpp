#include <windows.h>
#include <iostream>
#include <iomanip>
#include <string>

// Modbus RTU CRC16 Calculator
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

int main() {
    std::cout << "=================================================" << std::endl;
    std::cout << "  TAP-3001 Modbus Line Diagnostic Tool (19200 8N1) " << std::endl;
    std::cout << "=================================================" << std::endl;

    std::string portName = "\\\\.\\COM3";
    std::cout << "[INFO] Opening port COM3..." << std::endl;

    HANDLE hSerial = CreateFileA(
        portName.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "[ERROR] Failed to open port. Error code: " << GetLastError() << std::endl;
        return 1;
    }

    // Configure 19200 8N1
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hSerial, &dcb)) {
        std::cerr << "[ERROR] GetCommState failed." << std::endl;
        CloseHandle(hSerial);
        return 1;
    }

    dcb.BaudRate = CBR_19200;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity = NOPARITY;
    dcb.fDtrControl = DTR_CONTROL_ENABLE;
    dcb.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(hSerial, &dcb)) {
        std::cerr << "[ERROR] SetCommState failed." << std::endl;
        CloseHandle(hSerial);
        return 1;
    }

    // Configure timeouts (1 second read timeout)
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 1000;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 100;
    timeouts.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(hSerial, &timeouts);

    // Frame: Read Register 3 (index 2: AZCUR) from Slave 1
    // Slave=1, FC=03, Address=00 02, Quantity=00 01
    uint8_t frame[8] = { 0x01, 0x03, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00 };
    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    std::cout << "[INFO] Sending Modbus Request (Read Reg 3): ";
    for (int i = 0; i < 8; i++) {
        std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)frame[i] << " ";
    }
    std::cout << std::dec << std::endl;

    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    if (!WriteFile(hSerial, frame, 8, &bytesWritten, NULL) || bytesWritten != 8) {
        std::cerr << "[ERROR] Failed to write to serial port." << std::endl;
        CloseHandle(hSerial);
        return 1;
    }

    std::cout << "[INFO] Waiting for response..." << std::endl;

    uint8_t rxBuffer[256] = {0};
    DWORD bytesRead = 0;
    
    if (ReadFile(hSerial, rxBuffer, sizeof(rxBuffer), &bytesRead, NULL)) {
        if (bytesRead == 0) {
            std::cout << "[RESULT] TIMEOUT: Read 0 bytes from the serial line." << std::endl;
            std::cout << "         -> Check if the positioner is powered ON." << std::endl;
            std::cout << "         -> If using RS-485, try SWAPPING the A (Data+) and B (Data-) wires." << std::endl;
        } else {
            std::cout << "[RESULT] SUCCESS! Received " << bytesRead << " bytes:" << std::endl;
            std::cout << "         Hex: ";
            for (DWORD i = 0; i < bytesRead; i++) {
                std::cout << "0x" << std::hex << std::setw(2) << std::setfill('0') << (int)rxBuffer[i] << " ";
            }
            std::cout << std::dec << std::endl;

            // Verify CRC
            if (bytesRead >= 5) {
                uint16_t calcCrc = calculateCRC(rxBuffer, bytesRead - 2);
                uint16_t recCrc = rxBuffer[bytesRead - 2] | (rxBuffer[bytesRead - 1] << 8);
                if (calcCrc == recCrc) {
                    std::cout << "         [CRC OK] CRC matches!" << std::endl;
                    if (rxBuffer[1] == 0x03 && bytesRead == 7) {
                        int16_t val = (rxBuffer[3] << 8) | rxBuffer[4];
                        std::cout << "         Parsed Azimuth Current: " << (val / 10.0f) << " deg" << std::endl;
                    } else if (rxBuffer[1] & 0x80) {
                        std::cout << "         [EXCEPTION] Device replied with Modbus Exception: " << (int)rxBuffer[2] << std::endl;
                    }
                } else {
                    std::cout << "         [CRC ERROR] Calculated CRC: 0x" << std::hex << calcCrc 
                              << ", Received CRC: 0x" << recCrc << std::dec << std::endl;
                }
            }
        }
    } else {
        std::cerr << "[ERROR] ReadFile failed. Error code: " << GetLastError() << std::endl;
    }

    CloseHandle(hSerial);
    return 0;
}
