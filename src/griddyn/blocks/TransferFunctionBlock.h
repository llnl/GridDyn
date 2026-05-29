/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Block.h"
#include <string>
#include <vector>

namespace griddyn::blocks {
/** @brief class implementing a generic transfer unction
block implementing \f$H(S)=\frac{K(b_0+b_1 s +/hdots +b_n s^n}{a_0+a_1 s +/hdots +a_n s^n}\f$
it then converts it to observable canonical form as state space matrices for implementation as part
the solver

*/
class TransferFunctionBlock: public GridBlock {
  public:
  protected:
    std::vector<double> a;  //!< lower time constant
    std::vector<double> b;  //!< upper time constant
  private:
    // double rescale = 1;                   //!< containing the original $a_n$ for rescaling if
    // coefficients are changed later
    bool extraOutputState = false;  //!< flag indicating that there is an extra state
                                    //!< computation at the end due to direct dependence of B;
  public:
    /** constructor to add in the order of the transfer function
@param[in] order  the order of the transfer function
*/
    explicit TransferFunctionBlock(int order = 1);

    /** constructor to add in the name of the block
@param[in] objName  the name
*/
    explicit TransferFunctionBlock(const std::string& objName);
    /** constructor to define the transfer function coefficients assuming $b_0=1$ and all others
are 0
@param[in] acoef the denominator coefficients
*/
    explicit TransferFunctionBlock(std::vector<double> acoef);
    /** constructor to define the transfer function coefficients
@param[in] acoef the denominator coefficients
@param[in] bcoef the numerator coefficients
*/
    TransferFunctionBlock(std::vector<double> acoef, std::vector<double> bcoef);
    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void dynObjectInitializeA(coreTime time0, std::uint32_t flags) override;
    virtual void dynObjectInitializeB(const IOdata& inputs,
                                      const IOdata& desiredOutput,
                                      IOdata& fieldSet) override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual index_t findIndex(std::string_view field, const SolverMode& sMode) const override;

    virtual void blockDerivative(double input,
                                 double didt,
                                 const StateData& stateDataValue,
                                 double deriv[],
                                 const SolverMode& sMode) override;
    virtual void blockResidual(double input,
                               double didt,
                               const StateData& stateDataValue,
                               double resid[],
                               const SolverMode& sMode) override;
    // only called if the genModel is not present
    virtual void blockJacobianElements(double input,
                                       double didt,
                                       const StateData& stateDataValue,
                                       matrixData<double>& matrixDataValue,
                                       index_t argLoc,
                                       const SolverMode& sMode) override;
    virtual double step(coreTime time, double inputA) override;
    // virtual void setTime(coreTime time){prevTime=time;};
    virtual stringVec localStateNames() const override;
};
}  // namespace griddyn::blocks
