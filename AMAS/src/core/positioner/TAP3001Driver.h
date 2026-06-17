#ifndef TAP3001DRIVER_H
#define TAP3001DRIVER_H

#include "IPositionerDriver.h"
#include <windows.h>
#include <string>

namespace AMAS {

class TAP3001Driver : public IPositionerDriver {
public:
    TAP3001Driver();
    virtual ~TAP3001Driver();

    bool connect(const std::string& port) override;
    void disconnect() override;
    bool moveTo(float angle_deg) override;
    float getCurrentAngle() override;
    bool waitForSettle(int timeout_ms) override;
    void home() override;
    void emergencyStop() override;
    bool isConnected() const override;

    // Helper to query elevation and polarization if needed by the system
    float getElevationAngle();
    float getPolarizationAngle();
    bool moveElevation(float angle_deg);
    bool movePolarization(float angle_deg);

private:
    HANDLE m_hSerial;
    bool m_connected;
    uint8_t m_slaveId;
    float m_targetAngle;

    // Modbus utility methods
    uint16_t calculateCRC(const uint8_t* buf, int len) const;
    bool readRegs(uint16_t address, uint16_t numRegs, int16_t* outVals);
    bool writeReg(uint16_t address, int16_t val);
};

} // namespace AMAS

#endif // TAP3001DRIVER_H
