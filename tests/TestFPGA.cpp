/*
 * This file is part of LSST cRIOcpp test suite. Tests FPGA Cli App.
 *
 * Developed for the Vera C. Rubin Observatory Telescope & Site Software Systems.
 * This product includes software developed by the Vera C.Rubin Observatory Project
 * (https://www.lsst.org). See the COPYRIGHT file at the top-level directory of
 * this distribution for details of code ownership.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <catch2/catch_test_macros.hpp>
#include <string.h>

#include <TestFPGA.h>

using namespace LSST::cRIO;

uint16_t currentMode = 0;  // current address 8 mod

void TestILC::processChangeILCMode(uint8_t address, uint16_t mode) {
    REQUIRE(address == 8);
    REQUIRE(mode == currentMode);
}

TestFPGA::TestFPGA()
        : FPGA(fpgaType::SS),
          PrintILC(1),
          _response(),
          _U16ResponseStatus(IDLE),
          _pages(nullptr),
          _currentTimestamp(0) {}

void TestFPGA::writeCommandFIFO(uint16_t* data, size_t length, uint32_t timeout) {
    uint16_t* d = data;
    while (d < data + length) {
        size_t dl;
        switch (*d) {
            case FPGAAddress::MODBUS_A_TX:
                d++;
                dl = *d;
                d++;
                _simulateModbus(d, dl);
                d += dl;
                break;
            case FPGAAddress::HEARTBEAT:
                d += 2;
                break;
            // modbus software trigger
            case 252:
                d++;
                break;
            default:
                throw std::runtime_error(
                        "SimulatedFPGA::writeCommandFIFO unknown/unimplemented instruction: " +
                        std::to_string(*d));
                d++;
                break;
        }
    }
}

void TestFPGA::writeRequestFIFO(uint16_t* data, size_t length, uint32_t timeout) { _U16ResponseStatus = LEN; }

void TestFPGA::readU16ResponseFIFO(uint16_t* data, size_t length, uint32_t timeout) {
    switch (_U16ResponseStatus) {
        case IDLE:
            throw std::runtime_error("readU16ResponseFIFO called out of order");
        case LEN:
            REQUIRE(length == 1);
            *data = _response.getLength();
            _U16ResponseStatus = DATA;
            break;
        case DATA:
            REQUIRE(length == _response.getLength());
            memcpy(data, _response.getBuffer(), _response.getLength() * 2);
            _response.clear();
            _U16ResponseStatus = IDLE;
            break;
    }
}

void TestFPGA::processServerStatus(uint8_t address, uint8_t mode, uint16_t status, uint16_t faults) {
    _response.write(address);
    _response.write<uint8_t>(18);
    _response.write(mode);
    _response.write(status);
    _response.write(faults);

    _response.writeCRC();
}

void TestFPGA::processChangeILCMode(uint8_t address, uint16_t mode) {
    _response.write(address);
    _response.write<uint8_t>(65);
    _response.write(mode);

    _response.writeCRC();
}

void TestFPGA::processVerifyUserApplication(uint8_t address, uint16_t status) {
    _response.write(address);
    _response.write<uint8_t>(103);
    _response.write(status);

    _response.writeCRC();
}

void TestFPGA::_simulateModbus(uint16_t* data, size_t length) {
    // reply format:
    // 4 bytes (forming uint64_t in low endian) beginning timestamp
    // data received from ILCs (& FIFO::TX_WAIT_LONG_RX)
    // end of frame (FIFO::RX_ENDFRAME)
    // 8 bytes of end timestap (& FIFO::RX_TIMESTAMP)
    _response.writeFPGATimestamp(_currentTimestamp++);

    TestILC buf(1);
    buf.setBuffer(data, length);
    while (!buf.endOfBuffer()) {
        uint16_t p = buf.peek();
        if ((p & FIFO::CMD_MASK) != FIFO::WRITE) {
            buf.next();
            continue;
        }

        uint8_t address = buf.read<uint8_t>();
        REQUIRE(address == 8);
        uint8_t func = buf.read<uint8_t>();
        switch (func) {
            case 18:
                buf.checkCRC();
                processServerStatus(address, ILCMode::Standby, 0, 0);
                break;
            case 65:
                currentMode = buf.read<uint16_t>();
                buf.checkCRC();
                processChangeILCMode(address, currentMode);
                break;
            case 100: {
                uint16_t dataCRC = buf.read<uint16_t>();
                REQUIRE(dataCRC == 0x0495);
                uint16_t startAddress = buf.read<uint16_t>();
                REQUIRE(startAddress == 0);
                uint16_t length = buf.read<uint16_t>();
                REQUIRE(length == 67);
                uint16_t crc = buf.read<uint16_t>();
                REQUIRE(crc == 0x3BAB);
                buf.checkCRC();
                _ackFunction(address, 100);
                break;
            }

            case 101: {
                buf.checkCRC();
                _ackFunction(address, func);
                break;
            }

            case 102: {
                uint16_t startAddress = buf.read<uint16_t>();
                REQUIRE(startAddress == 0);
                uint16_t length = buf.read<uint16_t>();
                REQUIRE(length == 192);
                uint8_t fw[192];
                buf.readBuffer(fw, 192);
                for (size_t i = 0; i < length; i++) {
                    REQUIRE(fw[i] == _pages[i]);
                }
                buf.checkCRC();
                processWriteApplicationPage(address);
                break;
            }
            case 103: {
                buf.checkCRC();
                processVerifyUserApplication(address, 0);
                break;
            }
            default:
                throw std::runtime_error("Invalid function number " + std::to_string(func));
        }
        _response.writeRxTimestamp(_currentTimestamp++);

        _response.writeRxEndFrame();
    }
}

void TestFPGA::_ackFunction(uint8_t address, uint8_t func) {
    _response.write(address);
    _response.write(func);

    _response.writeCRC();
}
