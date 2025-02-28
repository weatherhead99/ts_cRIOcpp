/*
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

#ifndef CRIO_MODBUSBUFFER_H_
#define CRIO_MODBUSBUFFER_H_

#include <functional>
#include <map>
#include <queue>
#include <string>
#include <stdexcept>
#include <vector>

#include <arpa/inet.h>

#ifdef __APPLE__

#include <libkern/OSByteOrder.h>

#define htobe16(x) OSSwapHostToBigInt16(x)
#define htole16(x) OSSwapHostToLittleInt16(x)
#define be16toh(x) OSSwapBigToHostInt16(x)
#define le16toh(x) OSSwapLittleToHostInt16(x)

#define htobe32(x) OSSwapHostToBigInt32(x)
#define htole32(x) OSSwapHostToLittleInt32(x)
#define be32toh(x) OSSwapBigToHostInt32(x)
#define le32toh(x) OSSwapLittleToHostInt32(x)

#define htobe64(x) OSSwapHostToBigInt64(x)
#define htole64(x) OSSwapHostToLittleInt64(x)
#define be64toh(x) OSSwapBigToHostInt64(x)
#define le64toh(x) OSSwapLittleToHostInt64(x)

#else

#include <endian.h>

#include <cRIO/DataTypes.h>

#endif

namespace LSST {
namespace cRIO {

namespace FIFO {
// masks for FPGA FIFO commands
const static uint16_t WRITE = 0x1000;
const static uint16_t TX_FRAMEEND = 0x20DA;
const static uint16_t TX_TIMESTAMP = 0x3000;
const static uint16_t DELAY = 0x4000;
const static uint16_t LONG_DELAY = 0x5000;
const static uint16_t TX_WAIT_RX = 0x6000;
const static uint16_t TX_IRQTRIGGER = 0x7000;
const static uint16_t TX_WAIT_TRIGGER = 0x8000;
const static uint16_t TX_WAIT_LONG_RX = 0x9000;
const static uint16_t RX_ENDFRAME = 0xA000;
const static uint16_t RX_TIMESTAMP = 0xB000;
const static uint16_t CMD_MASK = 0xF000;

const static uint16_t TX_MASK = 0x1200;
const static uint16_t RX_MASK = 0x9200;
}  // namespace FIFO

/**
 * Utility class for Modbus buffer management.
 *
 * This class doesn't handle subnet. Doesn't handle FPGA FIFO read/writes -
 * that's responsibility of FPGA. ModbusBuffer handles only serialization & de
 * serialization of FPGA's FIFO data. Also this class handles CRC calculation,
 * writes (in output buffers) and checks (for input buffers).
 *
 * Replies received from ModBus shall be processed with ModbusBuffer::processResponse()
 * method. Virtual processXX methods are called to process received data.
 * Relations between functions and processing methods are established with
 * ModbusBuffer::addResponse().
 *
 * When processing data, the class guarantee that every non-broadcast call
 * generates reply at correct order - see ModbusBuffer::processResponse() for details.
 *
 *
 * This class also manages change detection. When recordChanges() is called,
 * all read data are (uint_8, uncoverted) are stored into separated buffer.
 * checkRecording(std::vector<uint8_t>&) can be called after all data are read
 * to verify the content is the same. This can be used to prevent events
 * functions being called when no change is detected (so there aren't multiple
 * SAL calls with events with same contents; shall not be used for telemetry
 * data, where even non-changed value shall be propagated)..
 *
 * Functions throws std::runtime_error (or its subclass) on any error.
 */
class ModbusBuffer {
public:
    /**
     * Constructs empty ModbusBuffer.
     */
    ModbusBuffer();

    /**
     * Construct ModbusBuffer from data
     *
     * @param buffer data buffer
     * @param length length of data buffer
     */
    ModbusBuffer(uint16_t* buffer, size_t length) { setBuffer(buffer, length); }

    virtual ~ModbusBuffer();

    /**
     * Returns buffer memory.  Calls to write* methods can result in
     * reallocation of the buffer memory, trying to read from getBuffer if that
     * happens results in undefined behavior.
     *
     * @return uint16_t FIFO buffer
     */
    uint16_t* getBuffer() { return _buffer.data(); }

    const std::vector<uint16_t> getBufferVector() { return _buffer; }

    /**
     * Returns buffer length.
     *
     * @return buffer length
     */
    size_t getLength() { return _buffer.size(); }

    /**
     * Resets internal offset to 0 to start reading message again. Clears
     * internal CRC counter, so CRC will be recalculated during subsequent
     * reads.
     */
    virtual void reset();

    /**
     * Clears modbus buffer.
     *
     * @param onlyBuffers if true, clears only buffers - function history (used
     * to make sure all queries are answered) isn't cleared. Defaults to false
     */
    void clear(bool onlyBuffers = false);

    bool endOfBuffer();
    bool endOfFrame();

    /**
     * Returns read data from buffer.
     *
     * @param length data length
     *
     * @return read data
     */
    std::vector<uint8_t> getReadData(int32_t length);

    /**
     * Reads data from buffer. Updates CRC as it reads the data. Size of
     * receiving buffer is assumed to be equal or greater than len.
     *
     * @param buf target buffer to read the data
     * @param len how many bytes shall be read
     */
    void readBuffer(void* buf, size_t len);

    /**
     * Peak current value in buffer. Do not increase the index.
     */
    uint16_t peek() { return _buffer[_index]; }

    /**
     * Ignore current word, move to next.
     */
    void next() {
        if (endOfBuffer()) {
            throw EndOfBuffer();
        }
        _index++;
    }

    /**
     * Template to read next data from message. Data length is specified with
     * template type. Handles conversion from ModBus/ILC's big endian (network order).
     *
     * Intended usage:
     *
     * @code{.cpp}
     * ModbusBuffer b;
     * b.setBuffer({(0x1200 | (0x0a << 1)), (0x1200 | (0x0c << 1)), (0x1200 | (0x0d << 1))}, 3);
     * uint8_t p1 = b.read<uint8_t>();
     * uint16_t p2 = b.read<uint16_t>();
     * @endcode
     *
     * @tparam dt variable data type. Supported are uint8_t, uint16_t,
     * uint32_t, uint64_t and int32_t and float.
     *
     * @return value of read response
     */
    template <typename dt>
    dt read();

    /**
     * Reads 6 bytes (48 bits) unsigned value.
     *
     * @return 6 bytes unsigned value read from the buffer
     */
    uint64_t readU48();

    /**
     * Reads string of given length from incoming buffer.
     *
     * @param length
     *
     * @return
     */
    std::string readString(size_t length);
    double readTimestamp();

    /**
     * Returns current calculcated CRC.
     *
     * @return calculated CRC
     */
    uint16_t getCalcCrc() { return _crc.get(); }

    /**
     * Check that accumulated data CRC matches readed CRC. Also stops recording
     * of changes, as CRC shall be at the end of buffer.
     *
     * @see pauseRecordChanges
     *
     * @throw CRCError if CRC doesn't match
     */
    void checkCRC();

    /**
     * Command FPGA to wait a bit for broadcast to be processed.
     *
     * @return delay in us (microseconds)
     *
     * @throw std::runtime_error if wait for delay command isn't present
     */
    uint32_t readDelay();

    /**
     * Check that next command is end of frame
     *
     * @throw std::runtime_error if end of frame isn't next buffer entry
     */
    virtual void readEndOfFrame() = 0;

    /**
     * Write uint8_t buffer to modbus, updates CRC.
     *
     * @param data
     * @param len
     */
    void writeBuffer(uint8_t* data, size_t len);

    /**
     * Write a value to buffer.
     *
     * @tparam dt value data type. Supported are uint8_t, uint16_t, uint32_t,
     * int8_t, int16_t, int32_t and float.
     *
     * @param data value to write to buffer
     */
    template <typename dt>
    void write(dt data);

    /**
     * Writes 24bit signed integer.
     *
     * @param data 24bit signed integer
     *
     * @see write
     */
    void writeI24(int32_t data);

    /**
     * Writes CRC for all data already written. Reset internal CRC counter, so
     * buffer can accept more commands.
     */
    void writeCRC();

    /**
     * Write FPGA delay command. Delays processing of read buffer by given
     * number of microseconds.
     *
     * @param delayMicros delay in microseconds
     */
    void writeDelay(uint32_t delayMicros);

    /**
     * Writes end of the frame. Causes silence on transmitting bus, so
     * commanded ModBus can check CRC of the incomming message and starts to
     * execute the commanded action. Should be overriden in children.
     */
    virtual void writeEndOfFrame() = 0;

    /**
     * Write FPGA Modbus command to wait for device response. If no response is
     * received within timeout,
     *
     * @param timeoutMicros
     */
    virtual void writeWaitForRx(uint32_t timeoutMicros) = 0;

    virtual void writeRxEndFrame() = 0;

    /**
     * Sets current read buffer
     *
     * @param buffer
     * @param length
     */
    void setBuffer(uint16_t* buffer, size_t length);

    /**
     * Checks that no more replies are expected.
     *
     * @throw std::runtime_error if commands to be processed are still expected
     */
    void checkCommandedEmpty();

    /**
     * Add response callbacks. Both function code and error response code shall
     * be specified.
     *
     * @param func callback for this function code
     * @param action action to call when the response is found. Passed address
     * as sole parameter. Should read response (as length of the response data
     * is specified by function) and check CRC (see ModbusBuffer::read and
     * ModbusBuffer::checkCRC)
     * @param errorResponse error response code
     * @param errorAction action to call when error is found. If no action is
     * specified, raises ModbusBuffer::Exception. Th action receives two parameters,
     * address and error code. CRC checking is done in processResponse. This
     * method shall not manipulate the buffer (e.g. shall not call
     * ModbusBuffer::read or ModbusBuffer::checkCRC).
     *
     * @see checkCached
     */
    void addResponse(uint8_t func, std::function<void(uint8_t)> action, uint8_t errorResponse,
                     std::function<void(uint8_t, uint8_t)> errorAction = nullptr);

    /**
     * Process received data. Reads function code, check CRC, check that the
     * function was called in request (using _commanded buffer) and calls
     * method to process data. Repeat until all data are processed.
     *
     * @note Can be called multiple times. Please call ModbusBuffer::checkCommandedEmpty
     * after all data are processed.
     *
     * @param response response includes response code (0x9) and start bit (need to >> 1 && 0xFF to get the
     * Modbus data)
     * @param length data length
     *
     * @throw std::runtime_error subclass on any detected error
     *
     * @see ModbusBuffer::checkCommandedEmpty()
     */
    void processResponse(uint16_t* response, size_t length);

    /**
     * Called before responses are processed (before processXX methods are
     * called).
     */
    virtual void preProcess(){};

    /**
     * Called after responses are processed (after processXX methods are
     * called).
     */
    virtual void postProcess(){};

    /**
     * Thrown when an unknown response function is received. As unknown function
     * response means unknown message length and hence unknown CRC position and
     * start of a new frame, the preferred handling when such error is seen is to
     * flush response buffer and send the queries again.
     */
    class UnknownResponse : public std::runtime_error {
    public:
        /**
         * Constructed with data available during response.
         *
         * @param address ModBus address
         * @param func ModBus function. Response for this function is unknown at the moment.
         */
        UnknownResponse(uint8_t address, uint8_t func);
    };

    /**
     * Class to calculate CRC. Example usage:
     *
     * @code{.cpp}
     * ModbusBuffer::CRC crc;
     *
     * for (uint8_t d = 0; d < 0xFF; d++) {
     *     crc.add(d);
     * }
     * std::cout << "Modbus CRC is " << crc.get() << "(0x" << << std::setw(4) << std::setfill('0')
     *     << std::hex << crc.get() << ")" << std::endl;
     * @endcode
     */
    class CRC {
    public:
        /**
         * Construct CRC class. The class is ready to accept add calls (reset
         * don't need to be called).
         */
        CRC() { reset(); }

        /**
         * Reset internal CRC counter.
         */
        void reset() { _crcCounter = 0xFFFF; }

        /**
         * Adds data to CRC buffer. Updates CRC value to match previous data.
         *
         * @param data data to be added
         */
        void add(uint8_t data);

        /**
         * Returns CRC value.
         *
         * @return buffer Modbus CRC
         */
        uint16_t get() { return _crcCounter; }

    private:
        uint16_t _crcCounter;
    };

    /**
     * Thrown when ModBus error response is received.
     */
    class Exception : public std::runtime_error {
    public:
        /**
         * The class is constructed when an Modbus's error response is received.
         *
         * @param address Modbus address
         * @param func Modbus (error) function received
         * @param exception exception code
         */
        Exception(uint8_t address, uint8_t func, uint8_t exception);
    };

    /**
     * Exception thrown when calculated CRC doesn't match received CRC.
     */
    class CRCError : public std::runtime_error {
    public:
        CRCError(uint16_t calculated, uint16_t received);
    };

    class EndOfBuffer : public std::runtime_error {
    public:
        EndOfBuffer();
    };

    class UnmatchedFunction : public std::runtime_error {
    public:
        UnmatchedFunction(uint8_t address, uint8_t function);
        UnmatchedFunction(uint8_t address, uint8_t function, uint8_t expectedAddress,
                          uint8_t expectedFunction);
    };

protected:
    uint16_t getCurrentBuffer() { return _buffer[_index]; }
    uint16_t getCurrentBufferAndInc() { return _buffer[_index++]; }
    size_t getCurrentIndex() { return _index; }

    void pushBuffer(uint16_t data) { _buffer.push_back(data); }
    void incIndex() { _index++; }
    void resetCRC() { _crc.reset(); }

    /**
     * Return data item to write to buffer. Updates CRC counter.
     *
     * @param data data to write.
     *
     * @return 16bit for command queue.
     */
    virtual uint16_t getByteInstruction(uint8_t data);

    void processDataCRC(uint8_t data);

    /**
     * Reads instruction byte from FPGA FIFO. Increases index after instruction is read.
     *
     * @throw EndOfBuffer if asking for instruction after end of the buffer
     *
     * @return byte written by the instruction. Start bit is removed.
     */
    virtual uint8_t readInstructionByte();

    /**
     * Add to buffer Modbus function. Assumes subnet, data lengths and triggers are
     * send by FPGA class. If non-broadcast address is passed, stores address
     * and function into _commanded buffer.
     *
     * @param address ModBus address on subnet
     * @param function ModBus function to call
     * @param timeout function call timeout (excluding transfer times) in us (microseconds)
     */
    void callFunction(uint8_t address, uint8_t function, uint32_t timeout);

    /**
     * Call Modbus function with one or more parameter.
     *
     * @see callFunction(uint8_t, uint8_t, uint32_t)
     *
     * @tparam dt parameter type
     * @param address ModBus address on subnet
     * @param function ModBus function to call
     * @param timeout function call timeout (excluding transfer time) in us (microseconds)
     * @param params function parameters
     */
    template <typename... dt>
    void callFunction(uint8_t address, uint8_t function, uint32_t timeout, const dt&... params) {
        write(address);
        write(function);
        _functionArguments(params...);
        writeCRC();
        writeEndOfFrame();
        writeWaitForRx(timeout);

        pushCommanded(address, function);
    }

    /**
     * Call broadcast function.
     *
     * @param address broadcast address. Shall be 0, 148, 149 or 250. Not checked if in correct range
     * @param function function to call
     * @param counter broadcast counter. ModBus provides method to retrieve this
     * in unicast function to verify the broadcast was received and processed
     * @param delay delay in us (microseconds) for broadcast processing. Bus will remain silence for this
     * number of us to allow ModBus process the broadcast function
     * @param data function parameters. Usually device's bus ID indexed array
     * of values to pass to the devices
     * @param dataLen number of parameters
     */
    void broadcastFunction(uint8_t address, uint8_t function, uint8_t counter, uint32_t delay, uint8_t* data,
                           size_t dataLen);

    /**
     * Checks that received response matches expected response or no more receive commands are expected.
     *
     * @param address ModBus address on subnet. Use broadcast (0) to check for no more replies expected
     * @param function ModBus function code; if check is performed for error response, must equal to called
     * function
     *
     * @throw UnmatchedFunction on error
     */
    void checkCommanded(uint8_t address, uint8_t function);

    /**
     * Records changes. Stores all read data into separate buffer. Buffer can
     * be compared to stored buffer using checkRecording.
     */
    void recordChanges() { _recordChanges = true; }

    /**
     * Temporary stop recording changes.
     */
    void pauseRecordChanges() { _recordChanges = false; }

    /**
     * Check if recorded values match cached (previous) values. If change is
     * detected, cached parameter is returned with new recorded content.
     * Recording of changes is stopped.
     *
     * @param cached values to compare
     *
     * @return true if cached equal what was read (with recording enabled)
     */
    bool checkRecording(std::vector<uint8_t>& cached);

    void pushCommanded(uint8_t address, uint8_t function);

private:
    std::vector<uint16_t> _buffer;

    /**
     * Current indes in the buffer.
     */
    size_t _index;

    CRC _crc;

    std::queue<std::pair<uint8_t, uint8_t>> _commanded;

    void _functionArguments() {}

    template <typename dp1, typename... dt>
    void _functionArguments(const dp1& p1, const dt&... args) {
        write<dp1>(p1);
        _functionArguments(args...);
    }

    bool _recordChanges;
    std::vector<uint8_t> _records;

    std::map<uint8_t, std::function<void(uint8_t)>> _actions;
    std::map<uint8_t, std::pair<uint8_t, std::function<void(uint8_t, uint8_t)>>> _errorActions;
};

template <>
inline int32_t ModbusBuffer::read() {
    int32_t db;
    readBuffer(&db, 4);
    return ntohl(db);
}

template <>
inline uint8_t ModbusBuffer::read() {
    uint8_t ret;
    readBuffer(&ret, 1);
    return ret;
}

template <>
inline uint16_t ModbusBuffer::read() {
    uint16_t db;
    readBuffer(&db, 2);
    return ntohs(db);
}

template <>
inline uint32_t ModbusBuffer::read() {
    uint32_t db;
    readBuffer(&db, 4);
    return ntohl(db);
}

template <>
inline uint64_t ModbusBuffer::read() {
    uint64_t db;
    readBuffer(&db, 8);
    return be64toh(db);
}

template <>
inline float ModbusBuffer::read() {
    uint32_t d = read<uint32_t>();
    float* db = reinterpret_cast<float*>(&d);
    return *db;
}

template <>
inline void ModbusBuffer::write(int8_t data) {
    writeBuffer(reinterpret_cast<uint8_t*>(&data), 1);
}

template <>
inline void ModbusBuffer::write(int16_t data) {
    int16_t d = htons(data);
    writeBuffer(reinterpret_cast<uint8_t*>(&d), 2);
}

template <>
inline void ModbusBuffer::write(int32_t data) {
    int32_t d = htonl(data);
    writeBuffer(reinterpret_cast<uint8_t*>(&d), 4);
}

template <>
inline void ModbusBuffer::write(uint8_t data) {
    writeBuffer(&data, 1);
}

template <>
inline void ModbusBuffer::write(uint16_t data) {
    uint16_t d = htons(data);
    writeBuffer(reinterpret_cast<uint8_t*>(&d), 2);
}

template <>
inline void ModbusBuffer::write(uint32_t data) {
    uint32_t d = htonl(data);
    writeBuffer(reinterpret_cast<uint8_t*>(&d), 4);
}

template <>
inline void ModbusBuffer::write(uint64_t data) {
    uint64_t d = htobe64(data);
    writeBuffer(reinterpret_cast<uint8_t*>(&d), 8);
}

template <>
inline void ModbusBuffer::write(float data) {
    uint32_t* db = reinterpret_cast<uint32_t*>(&data);
    write<uint32_t>(*db);
}

}  // namespace cRIO
}  // namespace LSST

#endif /* CRIO_MODBUSBUFFER_H_ */
