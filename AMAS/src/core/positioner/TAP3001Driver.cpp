#include "TAP3001Driver.h"
#include "Logger.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace AMAS {

TAP3001Driver::TAP3001Driver()
    : m_hSerial(INVALID_HANDLE_VALUE)
    , m_connected(false)
    , m_slaveId(1)
    , m_targetAngle(0.0f)
{
}

TAP3001Driver::~TAP3001Driver() {
    disconnect();
}

bool TAP3001Driver::connect(const std::string& port) {
    if (m_connected) {
        return true;
    }

    Log::info("TAP3001Driver: Attempting to connect to serial port " + port + "...");
    
    // Windows requires \\.\ prefix for port names (especially COM10+)
    std::string formattedPort = "\\\\.\\" + port;
    
    m_hSerial = CreateFileA(
        formattedPort.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (m_hSerial == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        Log::error("TAP3001Driver: Failed to open serial port. Windows Error: " + std::to_string(err));
        return false;
    }

    // Configure Port Parameters (9600 Baud, 8N1)
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);

    if (!GetCommState(m_hSerial, &dcbSerialParams)) {
        Log::error("TAP3001Driver: GetCommState failed.");
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;
    dcbSerialParams.fDtrControl = DTR_CONTROL_ENABLE;
    dcbSerialParams.fRtsControl = RTS_CONTROL_ENABLE;

    if (!SetCommState(m_hSerial, &dcbSerialParams)) {
        Log::error("TAP3001Driver: SetCommState failed.");
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    // Configure timeouts: Non-blocking with 500ms constant total timeout
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout = 50;
    timeouts.ReadTotalTimeoutConstant = 500;
    timeouts.ReadTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant = 50;
    timeouts.WriteTotalTimeoutMultiplier = 10;

    if (!SetCommTimeouts(m_hSerial, &timeouts)) {
        Log::error("TAP3001Driver: SetCommTimeouts failed.");
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    // Active Verification: Try to read Register 3 (AZCUR) to confirm positioner is communicating
    int16_t azVal = 0;
    if (!readRegs(3, 1, &azVal)) {
        Log::error("TAP3001Driver: Connection opened, but device failed to respond to Modbus query.");
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
        return false;
    }

    m_connected = true;
    m_targetAngle = azVal / 10.0f;
    Log::info("TAP3001Driver: Successfully connected. Current Azimuth: " + std::to_string(m_targetAngle) + " deg");
    return true;
}

void TAP3001Driver::disconnect() {
    if (m_hSerial != INVALID_HANDLE_VALUE) {
        Log::info("TAP3001Driver: Closing serial connection.");
        CloseHandle(m_hSerial);
        m_hSerial = INVALID_HANDLE_VALUE;
    }
    m_connected = false;
}

bool TAP3001Driver::moveTo(float angle_deg) {
    if (!m_connected) {
        Log::error("TAP3001Driver: Cannot move, not connected.");
        return false;
    }

    if (angle_deg < 0.0f || angle_deg > 360.0f) {
        Log::error("TAP3001Driver: Target angle out of range [0.0 - 360.0]: " + std::to_string(angle_deg));
        return false;
    }

    m_targetAngle = angle_deg;
    int16_t writeVal = (int16_t)(angle_deg * 10.0f);
    
    Log::info("TAP3001Driver: Moving Azimuth to " + std::to_string(angle_deg) + " deg (Register Value: " + std::to_string(writeVal) + ")");
    return writeReg(2, writeVal); // Register 2 is AZREF
}

float TAP3001Driver::getCurrentAngle() {
    if (!m_connected) {
        return 0.0f;
    }

    int16_t azVal = 0;
    if (readRegs(3, 1, &azVal)) { // Register 3 is AZCUR
        return azVal / 10.0f;
    }
    
    Log::warn("TAP3001Driver: Failed to read current Azimuth angle.");
    return 0.0f;
}

bool TAP3001Driver::waitForSettle(int timeout_ms) {
    if (!m_connected) {
        return false;
    }

    const int interval_ms = 100;
    int elapsed_ms = 0;
    const float tolerance = 0.2f;

    Log::info("TAP3001Driver: Waiting for positioner to settle at " + std::to_string(m_targetAngle) + " deg...");

    while (elapsed_ms < timeout_ms) {
        float current = getCurrentAngle();
        if (std::fabs(current - m_targetAngle) <= tolerance) {
            Log::info("TAP3001Driver: Positioner settled at " + std::to_string(current) + " deg.");
            return true;
        }
        Sleep(interval_ms);
        elapsed_ms += interval_ms;
    }

    Log::warn("TAP3001Driver: Timeout waiting for positioner to settle.");
    return false;
}

void TAP3001Driver::home() {
    Log::info("TAP3001Driver: Homing positioner (moving Azimuth to 0.0 deg)...");
    moveTo(0.0f);
}

void TAP3001Driver::emergencyStop() {
    if (!m_connected) return;
    
    Log::warn("TAP3001Driver: EMERGENCY STOP! Halting movement by setting target to current angle.");
    float current = getCurrentAngle();
    m_targetAngle = current;
    int16_t writeVal = (int16_t)(current * 10.0f);
    writeReg(2, writeVal); // Set AZREF to current AZ position immediately
}

bool TAP3001Driver::isConnected() const {
    return m_connected;
}

// Extra 3-Axis Functions
float TAP3001Driver::getElevationAngle() {
    if (!m_connected) return 0.0f;
    int16_t elVal = 0;
    if (readRegs(11, 1, &elVal)) { // Register 11 is ELCUR
        return elVal / 10.0f;
    }
    return 0.0f;
}

float TAP3001Driver::getPolarizationAngle() {
    if (!m_connected) return 0.0f;
    int16_t plVal = 0;
    if (readRegs(19, 1, &plVal)) { // Register 19 is PLCUR
        return plVal / 10.0f;
    }
    return 0.0f;
}

bool TAP3001Driver::moveElevation(float angle_deg) {
    if (!m_connected) return false;
    if (angle_deg < -90.0f || angle_deg > 90.0f) {
        Log::error("TAP3001Driver: Elevation target out of range [-90.0 - 90.0]: " + std::to_string(angle_deg));
        return false;
    }
    int16_t writeVal = (int16_t)(angle_deg * 10.0f);
    return writeReg(10, writeVal); // Register 10 is ELREF
}

bool TAP3001Driver::movePolarization(float angle_deg) {
    if (!m_connected) return false;
    if (angle_deg < 0.0f || angle_deg > 360.0f) {
        Log::error("TAP3001Driver: Polarization target out of range [0.0 - 360.0]: " + std::to_string(angle_deg));
        return false;
    }
    int16_t writeVal = (int16_t)(angle_deg * 10.0f);
    return writeReg(18, writeVal); // Register 18 is PLREF
}

// Modbus RTU CRC16 Calculator
uint16_t TAP3001Driver::calculateCRC(const uint8_t* buf, int len) const {
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

// Write Modbus Register (Function Code 06)
bool TAP3001Driver::writeReg(uint16_t reg, int16_t val) {
    if (m_hSerial == INVALID_HANDLE_VALUE) return false;

    // Modbus packets require 0-based protocol addresses
    uint16_t address = reg - 1;

    uint8_t frame[8];
    frame[0] = m_slaveId;
    frame[1] = 0x06;
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (val >> 8) & 0xFF;
    frame[5] = val & 0xFF;

    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    PurgeComm(m_hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    if (!WriteFile(m_hSerial, frame, 8, &bytesWritten, NULL) || bytesWritten != 8) {
        Log::error("TAP3001Driver: Modbus Write Register - Failed to write frame to port.");
        return false;
    }

    uint8_t response[8] = {0};
    DWORD bytesRead = 0;

    if (!ReadFile(m_hSerial, response, 8, &bytesRead, NULL)) {
        Log::error("TAP3001Driver: Modbus Write Register - ReadFile failed.");
        return false;
    }

    if (bytesRead != 8) {
        Log::error("TAP3001Driver: Modbus Write Register - Read timeout. Expected 8, got " + std::to_string(bytesRead));
        return false;
    }

    uint16_t respCrc = calculateCRC(response, 6);
    uint16_t respCrcReceived = response[6] | (response[7] << 8);
    if (respCrc != respCrcReceived) {
        Log::error("TAP3001Driver: Modbus Write Register - CRC mismatch.");
        return false;
    }

    if (response[1] & 0x80) {
        Log::error("TAP3001Driver: Modbus Write Register - Exception code received: " + std::to_string((int)response[2]));
        return false;
    }

    return true;
}

// Read Modbus Registers (Function Code 03)
bool TAP3001Driver::readRegs(uint16_t startReg, uint16_t numRegs, int16_t* outVals) {
    if (m_hSerial == INVALID_HANDLE_VALUE) return false;

    // Modbus packets require 0-based protocol addresses
    uint16_t address = startReg - 1;

    uint8_t frame[8];
    frame[0] = m_slaveId;
    frame[1] = 0x03;
    frame[2] = (address >> 8) & 0xFF;
    frame[3] = address & 0xFF;
    frame[4] = (numRegs >> 8) & 0xFF;
    frame[5] = numRegs & 0xFF;

    uint16_t crc = calculateCRC(frame, 6);
    frame[6] = crc & 0xFF;
    frame[7] = (crc >> 8) & 0xFF;

    PurgeComm(m_hSerial, PURGE_RXCLEAR | PURGE_TXCLEAR);

    DWORD bytesWritten = 0;
    if (!WriteFile(m_hSerial, frame, 8, &bytesWritten, NULL) || bytesWritten != 8) {
        Log::error("TAP3001Driver: Modbus Read Registers - Failed to write frame to port.");
        return false;
    }

    DWORD expectedBytes = 5 + 2 * numRegs;
    uint8_t response[256] = {0};
    DWORD bytesRead = 0;

    if (!ReadFile(m_hSerial, response, expectedBytes, &bytesRead, NULL)) {
        Log::error("TAP3001Driver: Modbus Read Registers - ReadFile failed.");
        return false;
    }

    if (bytesRead != expectedBytes) {
        Log::error("TAP3001Driver: Modbus Read Registers - Read timeout. Expected " + std::to_string(expectedBytes) + ", got " + std::to_string(bytesRead));
        return false;
    }

    uint16_t respCrc = calculateCRC(response, expectedBytes - 2);
    uint16_t respCrcReceived = response[expectedBytes - 2] | (response[expectedBytes - 1] << 8);
    if (respCrc != respCrcReceived) {
        Log::error("TAP3001Driver: Modbus Read Registers - CRC mismatch.");
        return false;
    }

    if (response[1] & 0x80) {
        Log::error("TAP3001Driver: Modbus Read Registers - Exception code received: " + std::to_string((int)response[2]));
        return false;
    }

    for (int i = 0; i < numRegs; i++) {
        outVals[i] = (int16_t)((response[3 + 2 * i] << 8) | response[4 + 2 * i]);
    }

    return true;
}

} // namespace AMAS
