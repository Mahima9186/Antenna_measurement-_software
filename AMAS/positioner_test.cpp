#include <windows.h>
#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <cstdint>

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

// Win32 Serial Helper: Open Port
HANDLE openPort(const std::string& portName, DWORD baudRate = 19200) {
    // Format port name for Windows (e.g. \\.\COM3)
    std::string formattedPort = "\\\\.\\" + portName;
    
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
        DWORD err = GetLastError();
        std::cerr << "[ERROR] Failed to open port " << portName 
                  << ". Windows Error code: " << err << std::endl;
        return INVALID_HANDLE_VALUE;
    }

    // Configure Port Settings
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "[ERROR] GetCommState failed." << std::endl;
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    dcbSerialParams.BaudRate = baudRate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        std::cerr << "[ERROR] SetCommState failed." << std::endl;
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    // Configure Timeouts (non-blocking reads with 500ms total timeout)
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(hSerial, &timeouts)) {
        std::cerr << "[ERROR] SetCommTimeouts failed." << std::endl;
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    return hSerial;
}

bool readRegisters(HANDLE hSerial, uint8_t slaveId, uint16_t address, uint16_t numRegisters, int16_t* outValues) {
    uint8_t frame[8];
    frame[0] = slaveId;
    frame[1] = 0x03;
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (numRegisters >> 8) & 0xFF;
    frame[5] = numRegisters & 0xFF;

    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    // Purge port buffers to ensure fresh communication
    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    if (!WriteFile(hSerial, frame, 8, &bytesWritten, NULL) || bytesWritten != 8) {
        std::cerr << "[ERROR] Modbus Read Request: Failed to write to serial port." << std::endl;
        return false;
    }

    // Expected response size: 1 (slaveId) + 1 (fn) + 1 (byteCount) + 2*numRegisters + 2 (crc)
    DWORD expectedBytes = 5 + 2 * numRegisters;
    uint8_t response[256] = {0};
    DWORD bytesRead = 0;

    if (!ReadFile(hSerial, response, expectedBytes, &bytesRead, NULL)) {
        std::cerr << "[ERROR] Modbus Read Response: ReadFile failed." << std::endl;
        return false;
    }

    if (bytesRead != expectedBytes) {
        std::cerr << "[ERROR] Modbus Read Response: Read timeout. Expected " 
                  << expectedBytes << " bytes, read " << bytesRead << "." << std::endl;
        return false;
    }

    // Verify CRC
    uint16_t respCrc = calculateCRC(response, expectedBytes - 2);
    uint16_t respCrcReceived = response[expectedBytes - 2] | (response[expectedBytes - 1] << 8);
    if (respCrc != respCrcReceived) {
        std::cerr << "[ERROR] Modbus Read Response: CRC mismatch." << std::endl;
        return false;
    }

    // Check for Modbus Exceptions
    if (response[1] & 0x80) {
        std::cerr << "[ERROR] Modbus Exception received. Exception code: " 
                  << (int)response[2] << std::endl;
        return false;
    }

    // Extract raw registers
    for (int i = 0; i < numRegisters; i++) {
        outValues[i] = (int16_t)((response[3 + 2 * i] << 8) | response[4 + 2 * i]);
    }

    return true;
}

// Modbus Function Code 06: Write Single Register
bool writeRegister(HANDLE hSerial, uint8_t slaveId, uint16_t address, int16_t value) {
    uint8_t frame[8];
    frame[0] = slaveId;
    frame[1] = 0x06;
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (value >> 8) & 0xFF;
    frame[5] = value & 0xFF;

    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    PurgeComm(hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    if (!WriteFile(hSerial, frame, 8, &bytesWritten, NULL) || bytesWritten != 8) {
        std::cerr << "[ERROR] Modbus Write Request: Failed to write to serial port." << std::endl;
        return false;
    }

    uint8_t response[8] = {0};
    DWORD bytesRead = 0;

    if (!ReadFile(hSerial, response, 8, &bytesRead, NULL)) {
        std::cerr << "[ERROR] Modbus Write Response: ReadFile failed." << std::endl;
        return false;
    }

    if (bytesRead != 8) {
        std::cerr << "[ERROR] Modbus Write Response: Read timeout. Expected 8 bytes, read " 
                  << bytesRead << "." << std::endl;
        return false;
    }

    // Verify CRC
    uint16_t respCrc = calculateCRC(response, 6);
    uint16_t respCrcReceived = response[6] | (response[7] << 8);
    if (respCrc != respCrcReceived) {
        std::cerr << "[ERROR] Modbus Write Response: CRC mismatch." << std::endl;
        return false;
    }

    // Check for exceptions
    if (response[1] & 0x80) {
        std::cerr << "[ERROR] Modbus Exception received. Exception code: " 
                  << (int)response[2] << std::endl;
        return false;
    }

    return true;
}

int main() {
    std::cout << "===========================================" << std::endl;
    std::cout << "      TAP-3001 Positioner Modbus Test      " << std::endl;
    std::cout << "===========================================" << std::endl;

    std::string portName = "COM3";
    std::cout << "Enter Serial Port Name [default: " << portName << "]: ";
    std::string inputPort;
    std::getline(std::cin, inputPort);
    if (!inputPort.empty()) {
        portName = inputPort;
    }

    uint8_t slaveId = 1;
    std::cout << "Enter Modbus Slave ID [default: 1]: ";
    std::string inputSlave;
    std::getline(std::cin, inputSlave);
    if (!inputSlave.empty()) {
        try {
            slaveId = (uint8_t)std::stoi(inputSlave);
        } catch(...) {
            std::cout << "Invalid Slave ID, using default 1." << std::endl;
        }
    }

    std::cout << "\n[INFO] Opening serial port " << portName << " at 19200 baud..." << std::endl;
    HANDLE hSerial = openPort(portName, 19200);
    if (hSerial == INVALID_HANDLE_VALUE) {
        std::cerr << "[FATAL] Unable to connect to positioner." << std::endl;
        return 1;
    }
    std::cout << "[SUCCESS] Connected to " << portName << " successfully!" << std::endl;

    while (true) {
        std::cout << "\n--- MENU ---" << std::endl;
        std::cout << "1. Read Current Coordinates (AZ, EL, PL)" << std::endl;
        std::cout << "2. Move Azimuth (AZREF)" << std::endl;
        std::cout << "3. Move Elevation (ELREF)" << std::endl;
        std::cout << "4. Move Polarization (PLREF)" << std::endl;
        std::cout << "5. Read Status Registers" << std::endl;
        std::cout << "6. Configure Speeds & Step Sizes" << std::endl;
        std::cout << "7. Exit" << std::endl;
        std::cout << "Select option [1-7]: ";

        std::string optionStr;
        std::getline(std::cin, optionStr);
        if (optionStr.empty()) continue;
        int option = 0;
        try { option = std::stoi(optionStr); } catch(...) { continue; }

        if (option == 1) {
            // Read coordinates: Registers 3 (AZCUR), 11 (ELCUR), 19 (PLCUR)
            // We can read them sequentially
            int16_t azVal = 0, elVal = 0, plVal = 0;
            bool success = true;

            if (!readRegisters(hSerial, slaveId, 3, 1, &azVal)) success = false;
            if (!readRegisters(hSerial, slaveId, 10, 1, &elVal)) success = false;
            if (!readRegisters(hSerial, slaveId, 18, 1, &plVal)) success = false;

            if (success) {
                float azDeg = azVal / 10.0f;
                float elDeg = elVal / 10.0f;
                float plDeg = plVal / 10.0f;

                std::cout << "\n[SUCCESS] Current Positioner Coordinates:" << std::endl;
                std::cout << "  Azimuth      (AZCUR): " << std::fixed << std::setprecision(1) << azDeg << " deg" << std::endl;
                std::cout << "  Elevation    (ELCUR): " << std::fixed << std::setprecision(1) << elDeg << " deg" << std::endl;
                std::cout << "  Polarization (PLCUR): " << std::fixed << std::setprecision(1) << plDeg << " deg" << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to query coordinates." << std::endl;
            }
        } 
        else if (option == 2) {
            std::cout << "Enter target Azimuth angle [0.0 to 360.0]: ";
            std::string targetStr;
            std::getline(std::cin, targetStr);
            if (!targetStr.empty()) {
                try {
                    float targetVal = std::stof(targetStr);
                    if (targetVal < 0.0f || targetVal > 360.0f) {
                        std::cout << "Out of range!" << std::endl;
                    } else {
                        int16_t writeVal = (int16_t)(targetVal * 10.0f);
                        std::cout << "[INFO] Writing " << writeVal << " to AZREF (Addr 3)..." << std::endl;
                        if (writeRegister(hSerial, slaveId, 3, writeVal)) {
                            std::cout << "[SUCCESS] Target angle set." << std::endl;
                        }
                    }
                } catch(...) {
                    std::cout << "Invalid input." << std::endl;
                }
            }
        } 
        else if (option == 3) {
            std::cout << "Enter target Elevation angle [-90.0 to 90.0]: ";
            std::string targetStr;
            std::getline(std::cin, targetStr);
            if (!targetStr.empty()) {
                try {
                    float targetVal = std::stof(targetStr);
                    if (targetVal < -90.0f || targetVal > 90.0f) {
                        std::cout << "Out of range!" << std::endl;
                    } else {
                        int16_t writeVal = (int16_t)(targetVal * 10.0f);
                        std::cout << "[INFO] Writing " << writeVal << " to ELREF (Addr 10)..." << std::endl;
                        if (writeRegister(hSerial, slaveId, 10, writeVal)) {
                            std::cout << "[SUCCESS] Target angle set." << std::endl;
                        }
                    }
                } catch(...) {
                    std::cout << "Invalid input." << std::endl;
                }
            }
        } 
        else if (option == 4) {
            std::cout << "Enter target Polarization angle [0.0 to 360.0]: ";
            std::string targetStr;
            std::getline(std::cin, targetStr);
            if (!targetStr.empty()) {
                try {
                    float targetVal = std::stof(targetStr);
                    if (targetVal < 0.0f || targetVal > 360.0f) {
                        std::cout << "Out of range!" << std::endl;
                    } else {
                        int16_t writeVal = (int16_t)(targetVal * 10.0f);
                        std::cout << "[INFO] Writing " << writeVal << " to PLREF (Addr 18)..." << std::endl;
                        if (writeRegister(hSerial, slaveId, 18, writeVal)) {
                            std::cout << "[SUCCESS] Target angle set." << std::endl;
                        }
                    }
                } catch(...) {
                    std::cout << "Invalid input." << std::endl;
                }
            }
        } 
        else if (option == 5) {
            // Read Status bits: Registers 1 (AZSTBITS), 9 (ELSTBITS), 17 (PLSTBITS)
            int16_t azStatus = 0, elStatus = 0, plStatus = 0;
            bool success = true;

            if (!readRegisters(hSerial, slaveId, 1, 1, &azStatus)) success = false;
            if (!readRegisters(hSerial, slaveId, 9, 1, &elStatus)) success = false;
            if (!readRegisters(hSerial, slaveId, 17, 1, &plStatus)) success = false;

            if (success) {
                std::cout << "\n[SUCCESS] Status Bits:" << std::endl;
                std::cout << "  AZSTBITS (Reg 1) : 0x" << std::hex << std::setw(4) << std::setfill('0') << azStatus << std::dec << std::endl;
                std::cout << "  ELSTBITS (Reg 9) : 0x" << std::hex << std::setw(4) << std::setfill('0') << elStatus << std::dec << std::endl;
                std::cout << "  PLSTBITS (Reg 17): 0x" << std::hex << std::setw(4) << std::setfill('0') << plStatus << std::dec << std::endl;
            } else {
                std::cerr << "[ERROR] Failed to query status registers." << std::endl;
            }
        }
        else if (option == 6) {
            std::cout << "\n--- Configuration Submenu ---" << std::endl;
            std::cout << "1. Set Azimuth Speed (Reg 38) [2.0 to 12.0]" << std::endl;
            std::cout << "2. Set Elevation Speed (Reg 46) [2.0 to 12.0]" << std::endl;
            std::cout << "3. Set Polarization Speed (Reg 54) [2.0 to 12.0]" << std::endl;
            std::cout << "Select config option [1-3]: ";
            std::string cfgOptStr;
            std::getline(std::cin, cfgOptStr);
            if (!cfgOptStr.empty()) {
                int cfgOpt = 0;
                try { cfgOpt = std::stoi(cfgOptStr); } catch(...) { continue; }
                
                uint16_t regAddr = 0;
                if (cfgOpt == 1) regAddr = 38;
                else if (cfgOpt == 2) regAddr = 46;
                else if (cfgOpt == 3) regAddr = 54;

                if (regAddr != 0) {
                    std::cout << "Enter speed value [2.0 to 12.0]: ";
                    std::string spdStr;
                    std::getline(std::cin, spdStr);
                    try {
                        float spdFloat = std::stof(spdStr);
                        if (spdFloat < 2.0f || spdFloat > 12.0f) {
                            std::cout << "Out of range!" << std::endl;
                        } else {
                            int16_t spdInt = (int16_t)(spdFloat * 10.0f);
                            if (writeRegister(hSerial, slaveId, regAddr, spdInt)) {
                                std::cout << "[SUCCESS] Speed updated." << std::endl;
                            }
                        }
                    } catch(...) {
                        std::cout << "Invalid speed value." << std::endl;
                    }
                }
            }
        }
        else if (option == 7) {
            break;
        }
    }

    std::cout << "[INFO] Closing serial port..." << std::endl;
    CloseHandle(hSerial);
    std::cout << "[INFO] Finished." << std::endl;
    return 0;
}
