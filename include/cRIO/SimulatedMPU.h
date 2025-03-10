/*
 * MPU simulator.
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

#ifndef CRIO_SIMULATEDMPU_H_
#define CRIO_SIMULATEDMPU_H_

#include <cRIO/MPU.h>

namespace LSST {
namespace cRIO {

class SimulatedMPU : public MPU {
public:
    SimulatedMPU() : MPU(0, 0) {}
    SimulatedMPU(uint16_t* buffer, size_t length) : MPU(0, 0) { setBuffer(buffer, length); }
};

}  // namespace cRIO
}  // namespace LSST

#endif  // CRIO_SIMULATEDMPU_H_
