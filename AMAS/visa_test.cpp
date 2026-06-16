#include <algorithm>
#include <cctype>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <visa.h>

double parseFrequency(const std::string &input) {
  if (input.empty())
    return 0.0;

  std::string clean = input;
  // Remove all whitespace characters
  clean.erase(std::remove_if(clean.begin(), clean.end(),
                             [](unsigned char c) { return std::isspace(c); }),
              clean.end());

  double val = 0.0;
  size_t idx = 0;
  try {
    val = std::stod(clean, &idx);
  } catch (...) {
    return 0.0;
  }

  std::string unit = clean.substr(idx);
  for (char &c : unit)
    c = std::toupper(c);

  if (unit == "GHZ" || unit == "G" || unit == "g") {
    val *= 1e9;
  } else if (unit == "MHZ" || unit == "M" || unit == "m") {
    val *= 1e6;
  } else if (unit == "KHZ" || unit == "K" || unit == "k") {
    val *= 1e3;
  }
  return val;
}

int main() {
  ViSession defaultRM = VI_NULL;
  ViSession instr = VI_NULL;
  ViStatus status = VI_SUCCESS;
  ViUInt32 retCnt = 0;
  unsigned char buffer[256];

  std::cout << "AMAS - Keysight VISA Connection Test" << std::endl;
  std::cout << "====================================" << std::endl;

  // 1. Open the Default Resource Manager
  status = viOpenDefaultRM(&defaultRM);
  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] Failed to open Default Resource Manager. Status: 0x"
              << std::hex << status << std::dec << std::endl;
    return 1;
  }
  std::cout << "[INFO] Successfully opened Default Resource Manager."
            << std::endl;

  // 2. Open Session to the VNA Instrument
  // Using the default GPIB address specified in the design doc:
  // GPIB0::18::INSTR
  const char *resourceString = "GPIB0::18::INSTR";
  std::cout << "[INFO] Attempting to connect to VNA at: " << resourceString
            << " ..." << std::endl;

  status = viOpen(defaultRM, (ViRsrc)resourceString, VI_NULL, VI_NULL, &instr);
  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] Failed to open session to " << resourceString
              << ". Status: 0x" << std::hex << status << std::dec << std::endl;
    std::cerr << "[TIP] Check if the Keysight VNA is powered on, connected via "
                 "GPIB/USB, and has address 18."
              << std::endl;
    viClose(defaultRM);
    return 1;
  }
  std::cout << "[INFO] Successfully connected to VNA." << std::endl;

  // 3. Set a timeout value (5000 ms = 5 seconds)
  status = viSetAttribute(instr, VI_ATTR_TMO_VALUE, 5000);
  if (status < VI_SUCCESS) {
    std::cerr << "[WARNING] Failed to set timeout attribute. Status: 0x"
              << std::hex << status << std::dec << std::endl;
  }

  // 4. Send Identification Query (*IDN?)
  const char *idnCmd = "*IDN?\n";
  std::cout << "[INFO] Sending query: *IDN?" << std::endl;
  status = viWrite(instr, (ViBuf)idnCmd, (ViUInt32)strlen(idnCmd), &retCnt);
  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] Failed to write command to VNA. Status: 0x"
              << std::hex << status << std::dec << std::endl;
    viClose(instr);
    viClose(defaultRM);
    return 1;
  }

  // 5. Read response
  std::memset(buffer, 0, sizeof(buffer));
  status = viRead(instr, (ViBuf)buffer, sizeof(buffer) - 1, &retCnt);
  if (status < VI_SUCCESS) {
    std::cerr << "[ERROR] Failed to read response from VNA. Status: 0x"
              << std::hex << status << std::dec << std::endl;
  } else {
    std::cout << "[SUCCESS] VNA Identification String received:" << std::endl;
    std::cout << "          " << buffer << std::endl;
  }

  // 5.5. Configure Start Frequency, Stop Frequency, and Span
  auto checkInstrumentErrors = [&](const std::string &context) {
    const char *errQuery = ":SYST:ERR?\n";
    ViStatus errStatus =
        viWrite(instr, (ViBuf)errQuery, (ViUInt32)strlen(errQuery), &retCnt);
    if (errStatus >= VI_SUCCESS) {
      std::memset(buffer, 0, sizeof(buffer));
      errStatus = viRead(instr, (ViBuf)buffer, sizeof(buffer) - 1, &retCnt);
      if (errStatus >= VI_SUCCESS) {
        std::string errStr(reinterpret_cast<char *>(buffer));
        // Trim trailing whitespaces
        while (!errStr.empty() &&
               (errStr.back() == '\n' || errStr.back() == '\r' ||
                errStr.back() == ' ')) {
          errStr.pop_back();
        }
        // If it is not "+0, \"No error\"", print it
        if (errStr.find("+0") == std::string::npos &&
            errStr.find("No error") == std::string::npos) {
          std::cerr << "[ANALYZER ERROR] " << context << " failed: " << errStr
                    << std::endl;
        }
      }
    }
  };

  std::cout << "\n[E4407B Frequency Range: 9 kHz to 26.5 GHz]" << std::endl;

  std::string inputStartFreq;
  std::cout << "Enter desired Start Frequency (e.g., 4GHz, 9kHz): ";
  std::getline(std::cin, inputStartFreq);

  std::string inputStopFreq;
  std::cout << "Enter desired Stop Frequency (e.g., 6GHz, 26.5GHz): ";
  std::getline(std::cin, inputStopFreq);

  std::string inputPoints;
  std::cout << "Enter desired Sweep Points (e.g., 401, 1001, or leave blank): ";
  std::getline(std::cin, inputPoints);

  std::string inputSweepTime;
  std::cout
      << "Enter desired Sweep Time (e.g., 100ms, 0.05, AUTO, or leave blank): ";
  std::getline(std::cin, inputSweepTime);

  std::string inputRBW;
  std::cout << "Enter desired Resolution Bandwidth (RBW) (e.g., 100kHz, 10kHz, "
               "AUTO, or leave blank): ";
  std::getline(std::cin, inputRBW);

  std::string inputVBW;
  std::cout << "Enter desired Video Bandwidth (VBW) (e.g., 100kHz, 10kHz, "
               "AUTO, or leave blank): ";
  std::getline(std::cin, inputVBW);

  // Calculate Center and Span if inputs are provided
  double startHz = parseFrequency(inputStartFreq);
  double stopHz = parseFrequency(inputStopFreq);

  // E4407B Frequency Limits
  const double MIN_FREQ = 9.0e3;  // 9 kHz
  const double MAX_FREQ = 26.5e9; // 26.5 GHz

  if (!inputStartFreq.empty() && (startHz < MIN_FREQ || startHz > MAX_FREQ)) {
    std::cout << "[WARNING] Start Frequency is out of E4407B hardware range (9 "
                 "kHz to 26.5 GHz)!"
              << std::endl;
  }
  if (!inputStopFreq.empty() && (stopHz < MIN_FREQ || stopHz > MAX_FREQ)) {
    std::cout << "[WARNING] Stop Frequency is out of E4407B hardware range (9 "
                 "kHz to 26.5 GHz)!"
              << std::endl;
  }
  if (!inputStartFreq.empty() && !inputStopFreq.empty() && startHz >= stopHz) {
    std::cout << "[WARNING] Start Frequency must be less than Stop Frequency!"
              << std::endl;
  }

  if (startHz > 0.0 && stopHz > 0.0 && startHz < stopHz) {
    double calculatedCenterHz = (startHz + stopHz) / 2.0;
    double calculatedSpanHz = stopHz - startHz;
    std::cout
        << "\n[CALCULATION] Calculated parameters based on Start/Stop inputs:"
        << std::endl;
    std::cout << "  Calculated Center Frequency : " << std::fixed
              << std::setprecision(1) << calculatedCenterHz << " Hz ("
              << (calculatedCenterHz / 1e9) << " GHz)" << std::endl;
    std::cout << "  Calculated Span (Sweep Width): " << calculatedSpanHz
              << " Hz (" << (calculatedSpanHz / 1e6) << " MHz)" << std::endl;
  }

  // Set Start Frequency on instrument
  if (!inputStartFreq.empty()) {
    std::string cmd = ":SENS:FREQ:STAR " + inputStartFreq + "\n";
    std::cout << "\n[INFO] Setting start frequency to: " << inputStartFreq
              << "..." << std::endl;
    status =
        viWrite(instr, (ViBuf)cmd.c_str(), (ViUInt32)cmd.length(), &retCnt);
    if (status < VI_SUCCESS) {
      std::cerr << "[ERROR] Failed to set start frequency. Status: 0x"
                << std::hex << status << std::dec << std::endl;
    }
    checkInstrumentErrors("Setting Start Freq (" + inputStartFreq + ")");
  }

  // Set Stop Frequency on instrument
  if (!inputStopFreq.empty()) {
    std::string cmd = ":SENS:FREQ:STOP " + inputStopFreq + "\n";
    std::cout << "[INFO] Setting stop frequency to: " << inputStopFreq << "..."
              << std::endl;
    status =
        viWrite(instr, (ViBuf)cmd.c_str(), (ViUInt32)cmd.length(), &retCnt);
    if (status < VI_SUCCESS) {
      std::cerr << "[ERROR] Failed to set stop frequency. Status: 0x"
                << std::hex << status << std::dec << std::endl;
    }
    checkInstrumentErrors("Setting Stop Freq (" + inputStopFreq + ")");
  }

  // Set Sweep Points on instrument
  if (!inputPoints.empty()) {
    std::string cmd = ":SENS:SWE:POIN " + inputPoints + "\n";
    std::cout << "[INFO] Setting sweep points to: " << inputPoints << "..."
              << std::endl;
    status =
        viWrite(instr, (ViBuf)cmd.c_str(), (ViUInt32)cmd.length(), &retCnt);
    if (status < VI_SUCCESS) {
      std::cerr << "[ERROR] Failed to set sweep points. Status: 0x" << std::hex
                << status << std::dec << std::endl;
    }
    checkInstrumentErrors("Setting Sweep Points (" + inputPoints + ")");
  }

  // Set Sweep Time on instrument
  if (!inputSweepTime.empty()) {
    std::string cmd = ":SENS:SWE:TIME " + inputSweepTime + "\n";
    std::cout << "[INFO] Setting sweep time to: " << inputSweepTime << "..."
              << std::endl;
    status =
        viWrite(instr, (ViBuf)cmd.c_str(), (ViUInt32)cmd.length(), &retCnt);
    if (status < VI_SUCCESS) {
      std::cerr << "[ERROR] Failed to set sweep time. Status: 0x" << std::hex
                << status << std::dec << std::endl;
    }
    checkInstrumentErrors("Setting Sweep Time (" + inputSweepTime + ")");
  }

  // Set Resolution Bandwidth (RBW) on instrument
  if (!inputRBW.empty()) {
    std::string cmd;
    std::string upperRBW = inputRBW;
    for (char &c : upperRBW)
      c = std::toupper(c);
    if (upperRBW == "AUTO") {
      cmd = ":SENS:BAND:RES:AUTO ON\n";
    } else {
      cmd = ":SENS:BAND:RES " + inputRBW + "\n";
    }
    std::cout << "[INFO] Setting Resolution Bandwidth (RBW) to: " << inputRBW
              << "..." << std::endl;
    status =
        viWrite(instr, (ViBuf)cmd.c_str(), (ViUInt32)cmd.length(), &retCnt);
    if (status < VI_SUCCESS) {
      std::cerr
          << "[ERROR] Failed to set Resolution Bandwidth (RBW). Status: 0x"
          << std::hex << status << std::dec << std::endl;
    }
    checkInstrumentErrors("Setting RBW (" + inputRBW + ")");
  }

  // Set Video Bandwidth (VBW) on instrument
  if (!inputVBW.empty()) {
    std::string cmd;
    std::string upperVBW = inputVBW;
    for (char &c : upperVBW)
      c = std::toupper(c);
    if (upperVBW == "AUTO") {
      cmd = ":SENS:BAND:VID:AUTO ON\n";
    } else {
      cmd = ":SENS:BAND:VID " + inputVBW + "\n";
    }
    std::cout << "[INFO] Setting Video Bandwidth (VBW) to: " << inputVBW
              << "..." << std::endl;
    status =
        viWrite(instr, (ViBuf)cmd.c_str(), (ViUInt32)cmd.length(), &retCnt);
    if (status < VI_SUCCESS) {
      std::cerr
          << "[ERROR] Failed to set Video Bandwidth (VBW). Status: 0x"
          << std::hex << status << std::dec << std::endl;
    }
    checkInstrumentErrors("Setting VBW (" + inputVBW + ")");
  }

  // Verify values back by querying
  auto queryAndPrint = [&](const char *queryCmd, const char *label,
                           const std::string &unit) {
    ViStatus qStatus =
        viWrite(instr, (ViBuf)queryCmd, (ViUInt32)strlen(queryCmd), &retCnt);
    if (qStatus >= VI_SUCCESS) {
      std::memset(buffer, 0, sizeof(buffer));
      qStatus = viRead(instr, (ViBuf)buffer, sizeof(buffer) - 1, &retCnt);
      if (qStatus >= VI_SUCCESS) {
        // Trim trailing newlines/spaces if present for neat printing
        std::string resStr(reinterpret_cast<char *>(buffer));
        while (!resStr.empty() &&
               (resStr.back() == '\n' || resStr.back() == '\r' ||
                resStr.back() == ' ')) {
          resStr.pop_back();
        }
        std::cout << "[SUCCESS] verified " << label << ": " << resStr;
        if (!unit.empty())
          std::cout << " " << unit;
        std::cout << std::endl;
      } else {
        std::cerr << "[ERROR] Failed to read query response for " << label
                  << std::endl;
      }
    } else {
      std::cerr << "[ERROR] Failed to query " << label << std::endl;
    }
  };

  std::cout << "\n--- Verifying Analyzer Settings ---" << std::endl;
  queryAndPrint(":SENS:FREQ:CENT?\n", "Center Frequency", "Hz");
  queryAndPrint(":SENS:FREQ:STAR?\n", "Start Frequency", "Hz");
  queryAndPrint(":SENS:FREQ:STOP?\n", "Stop Frequency", "Hz");
  queryAndPrint(":SENS:FREQ:SPAN?\n", "Span", "Hz");
  queryAndPrint(":SENS:SWE:POIN?\n", "Sweep Points", "");
  queryAndPrint(":SENS:SWE:TIME?\n", "Sweep Time", "s");
  queryAndPrint(":SENS:BAND:RES?\n", "Resolution Bandwidth (RBW)", "Hz");
  queryAndPrint(":SENS:BAND:VID?\n", "Video Bandwidth (VBW)", "Hz");

  // 6. Clean up resources
  std::cout << "[INFO] Closing sessions..." << std::endl;
  viClose(instr);
  viClose(defaultRM);
  std::cout << "[INFO] Finished." << std::endl;

  return 0;
}
