/*
 * Electromechanical and Pneumatic ILC functions.
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

#ifndef _cRIO_ElectromechanicalPneumaticILC_h
#define _cRIO_ElectromechanicalPneumaticILC_h

#include <cRIO/ILC.h>

namespace LSST {
namespace cRIO {

/**
 * Class for communication with Electromechanical and Pneumatic ILCs.
 *
 * Replies received from ILCs shall be processed with ILC::processResponse method.
 */
class ElectromechanicalPneumaticILC : public virtual ILC {
public:
    /**
     * Populate responses for known ILC functions.
     *
     * @param bus ILC bus number (1..). Defaults to 1.
     */
    ElectromechanicalPneumaticILC(uint8_t bus = 1);

    /**
     * Unicast Hardpoint ILC Force [N] and Status Request. ILC command code 67 (0x43)
     *
     * @param address ILC address
     */
    void reportHardpointForceStatus(uint8_t address) { callFunction(address, 67, 1800); }

    /**
     * Unicast ADC Channel Offset and Sensitivity. ILC command code 81 (0x51)
     *
     * @param address ILC address
     * @param channel ADC channel (1-4)
     * @param offset ADC offset value
     * @param sensitivity ADC sensitivity value
     */
    void setOffsetAndSensitivity(uint8_t address, uint8_t channel, float offset, float sensitivity) {
        callFunction(address, 81, 36500, channel, offset, sensitivity);
    }

    /**
     * Read ILC calibration data. ILC command code 110 (0x6E).
     *
     * @param address ILC address
     */
    void reportCalibrationData(uint8_t address) { callFunction(address, 110, 1800); }

    /**
     * Read ILC mezzanine pressure. ILC command code 119 (0x77).
     *
     * @param address ILC address
     */
    void reportMezzaninePressure(uint8_t address) { callFunction(address, 119, 1800); }

protected:
    /**
     * Called when response from call to command 67 (0x43) is read.
     *
     * @param address returned from this ILC
     * @param status hardpoint Status
     * @param encoderPostion HP encoder position
     * @param loadCellForce measured load cell/actuator force
     */
    virtual void processHardpointForceStatus(uint8_t address, uint8_t status, int32_t encoderPostion,
                                             float loadCellForce) = 0;

    /**
     * Called when response from call to command 110 (0x6E) is read.
     *
     * @param address status returned from this ILC
     * @param mainADCK[4] main ADC calibration Kn
     * @param mainOffset[4] main sensor n offset
     * @param mainSensitivity[4] main sensor n sensitivity
     * @param backupADCK[4] backup ADC calibration Kn
     * @param backupOffset[4] backup sensor n offset
     * @param backupSensitivity[4] backup sensor n sensitivity
     */
    virtual void processCalibrationData(uint8_t address, float mainADCK[4], float mainOffset[4],
                                        float mainSensitivity[4], float backupADCK[4], float backupOffset[4],
                                        float backupSensitivity[4]) = 0;

    virtual void processMezzaninePressure(uint8_t address, float primaryPush, float primaryPull,
                                          float secondaryPush, float secondaryPull) = 0;
};

}  // namespace cRIO
}  // namespace LSST

#endif  //! _cRIO_ElectromechanicalPneumaticILC_h
