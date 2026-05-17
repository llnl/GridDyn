/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Link.h"
#include <string>
namespace griddyn {
#define APPROXIMATION_LEVELS (9)

/** class defining some computed information for links*/
class linkC {
  public:
    double cosTheta1 = 0.0;  //!<  cos theta1
    double cosTheta2 = 0.0;  //!<  cos theta2
    double sinTheta1 = 0.0;  //!<  sin theta1
    double sinTheta2 = 0.0;  //!<  sin theta2
    double Vmx = 0.0;  //!< computed parameter
};

/** @brief structure containing information on the partial derivatives for the the link
the seqID is also the index of the state data it was calculated from*/
typedef struct linkPartialDerivatives {
    double dP1dv1 = 0.0;
    double dP1dt1 = 0.0;
    double dQ1dv1 = 0.0;
    double dQ1dt1 = 0.0;
    double dQ1dv2 = 0.0;
    double dP1dv2 = 0.0;
    double dP1dt2 = 0.0;
    double dQ1dt2 = 0.0;

    double dP2dv1 = 0.0;
    double dP2dt1 = 0.0;
    double dQ2dv1 = 0.0;
    double dQ2dt1 = 0.0;
    double dQ2dv2 = 0.0;
    double dP2dv2 = 0.0;
    double dP2dt2 = 0.0;
    double dQ2dt2 = 0.0;
    index_t seqID = 0;
} linkPart;

/** @brief the class that links multiple nodes(buses) together
*  the base class for objects which connect other objects mainly buses
it implements the basic transmission model

Each link has a disconnect switch at the "from" bus and the "to" bus
*/
class acLine: public Link {
  public:
    enum acLine_flags {
        // indicator that the angle slipped past 90 degree on a test
        angle_slip_on_test = object_flag10,
    };

  protected:
    model_parameter mp_B{0.0};  //!< [pu] per unit shunt capacitance (jb/2 on each end of the line)
    model_parameter mp_G{0.0};  //!< [pu] per unit shunt conductance (g/2 on each end of the line)
    model_parameter fault{-1.0};  //!< fault location along the line keep at <0 for no fault
    double g{0.0};  //!< [pu] per unit conductance (calculated parameter)
    double b{0.0};  //!< [pu] per unit susceptance (calculated parameter)
    model_parameter tap{1.0};  //!< tap position, neutral t = 1;
    model_parameter tapAngle{0.0};  //!< [deg] phase angle for phase shifting transformer

    model_parameter minAngle{-kPI / 2.0};  //!< the minimum angle of the link can handle
    model_parameter maxAngle =
        kPI / 2.0;  //!< the maximum angle the link can handle--related to rating
    model_parameter length{0.0};  //!< [km] transmission line length
    model_parameter r{0.0};  //!< [pu] per unit resistance
    model_parameter x{0.00000001};  //!< [pu] per unit reactance
    linkI constLinkInfo;  //!< holder for static link bus information
    linkC linkComp;  //!< holder for some computed information
    linkPart LinkDeriv;  //!< holder for computed derivative information
    linkC constLinkComp;  //!< holder for some computed information

    linkF constLinkFlows;  //!< holder for previous steady state link flows

    using glMP = void (acLine::*)();
    glMP flowCalc[APPROXIMATION_LEVELS];  //!< function pointers to power flow calculations
    glMP derivCalc[APPROXIMATION_LEVELS];  //!< function objects to the derivative calculations

  public:
    /** @brief default constructor*/
    explicit acLine(const std::string& objName = "acline_$");
    /** @brief constructor specifying the real and imaginary part of the impedance
    @param[in] rP  the real impedance in pu ohm
    @param[in] xP  the reactance in pu Ohm
    @param[in] objName the name of the link
    */
    acLine(double rP, double xP, const std::string& objName = "acline_$");
    /** @brief virtual destructor*/

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;

    /** @brief get the current tap value
     * @return the tap value
     */
    double getTap() const { return tap; }
    /** @brief get the current tap angle value
     * @return the tap angle value
     */
    double getTapAngle() const { return tapAngle; }

    void disable() override;
    /** @brief allow the real power flow to be fixed by adjusting the properties of one bus or
    another performs the calculations necessary to get the power at the measureTerminal to be a
    certain value
    @param[in] power  the desired real power flow as measured by measureTerminal
    @param[in] measureTerminal  the measure terminal-either a terminal number (1 or higher) or a
    busID,  1 by default
    @param[in] fixedTerminal -the terminal that doesn't change (terminal number or busID) if 0 both
    are changed or 1 is selected based on busTypes
    @param[in] unitType -- the units related to power
    @return 0 for success, some other number for failure
    */
    virtual int fixRealPower(double power,
                             id_type_t measureTerminal,
                             id_type_t fixedTerminal = 0,
                             units::unit unitType = units::defunit) override;

    /** @brief allow the power flow to be fixed by adjusting the properties of one bus or another
     performs the calculations necessary to get the power at the measureTerminal to be a certain
    value
    @param[in] rPower  the desired real power flow as measured by measureTerminal
    @param[in] qPower  the desired reactive power flow as measured by measureTerminal
    @param[in] measureTerminal  the measure terminal-either a terminal number (1 or higher) or a
    busID,  1 by default
    @param[in] fixedTerminal -the terminal that doesn't change (terminal number or busID) if 0 both
    are changed or 1 is selected based on busTypes
    @param[in] unitType -- the units related to power
    @return 0 for success, some other number for failure
    */
    virtual int fixPower(double rPower,
                         double qPower,
                         id_type_t measureTerminal,
                         id_type_t fixedTerminal = 0,
                         units::unit unitType = units::defunit) override;

    /** @brief check for any violations of link limits or other factors based on power flow results
     checks things like the maximum angle,  power flow /current limits based on ratings and a few
    other things
    @param[out] Violation_vector --a list of all the violations any new violations get added to the
    result
    */
    virtual void pFlowCheck(std::vector<Violation>& Violation_vector) override;
    virtual change_code
        powerFlowAdjust(const IOdata& inputs, std::uint32_t flags, check_level_t level) override;
    virtual void pFlowObjectInitializeB() override;
    virtual void updateLocalCache() override;
    virtual void updateLocalCache(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode) override;

    virtual void timestep(coreTime time, const IOdata& inputs, const solverMode& sMode) override;
    /** @brief do a quick update  (may be deprecated)
     * @return the power transfer
     */
    virtual double quickupdateP() override;

    using Link::getAngle;
    virtual double getAngle(const double state[], const solverMode& sMode) const override;

    virtual void getParameterStrings(stringVec& pstr, paramStringType pstype) const override;
    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;
    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;

    /** @brief check if two buses should be merged and the line effects ignored
     */
    virtual void checkMerge() override;

    // for computing all the Jacobian elements at once
    using Link::ioPartialDerivatives;
    virtual void ioPartialDerivatives(id_type_t busId,
                                      const stateData& sD,
                                      matrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const solverMode& sMode) override;

    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode) override;
    virtual void outputPartialDerivatives(id_type_t busId,
                                          const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const solverMode& sMode) const override;
    virtual double getMaxTransfer() const override;
    // virtual void busResidual(index_t busId, const stateData &sD, double *Fp, double *Fq, const
    // solverMode &sMode);
    virtual void setState(coreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const solverMode& sMode) override;

    virtual change_code rootCheck(const IOdata& inputs,
                                  const stateData& sD,
                                  const solverMode& sMode,
                                  check_level_t level) override;

    virtual bool testAndTrip(int tripLevel) override;

  protected:
    void setAdmit();
    // virtual void basePowerComp ();
    /** @brief calculations for fault conditions
     */
    void faultCalc();
    /** @brief full ac calculations
     */
    void fullCalc();
    /** @brief assumes r is small and can be ignored
     */
    void simplifiedCalc();
    /** @brief assumes there is no coupling between P -theta and V-Q
     */
    void decoupledCalc();
    /** @brief assumes the r is small and decoupled
     */
    void simplifiedDecoupledCalc();
    /** @brief assumes the angle is small
     */
    void smallAngleCalc();
    /** @brief assumes the angle is small and r is small
     */
    void smallAngleSimplifiedCalc();
    /** @brief small angle decoupled
     */
    void smallAngleDecoupledCalc();
    /** @brief linearizes computations around previous operating point
     */
    void linearCalc();
    /** @brief computes the values for nonConnected line
     */
    void swOpenCalc();
    /** @brief assumes r is small and the angle is small and the voltage is constant for real power
    computation and angle is constant for reactive power computation so there is no cross coupling
    between V and theta*/
    void fastDecoupledCalc();
    /** @brief  fault Derivative Calculations
     */
    void faultDeriv();
    /** @brief  full ac calculations for partial derivatives
     */
    void fullDeriv();
    /** @brief assumes r is small and can be ignored for partial derivatives
     */
    void simplifiedDeriv();
    /** @brief assumes there is no coupling between P -theta and V-Q for partial derivatives
     */
    void decoupledDeriv();
    /** @brief assumes the r is small and decoupled
     */
    void simplifiedDecoupledDeriv();
    /** @brief assumes the angle is small
     */
    void smallAngleDeriv();
    /** @brief small angle decoupled
     */
    void smallAngleDecoupledDeriv();
    /** @brief assumes the angle is small and r is small
     */
    void smallAngleSimplifiedDeriv();
    /** @brief linearizes computations around previous operating point
     */
    void linearDeriv();
    /** @brief assumes r is small and the angle is small and the voltage is constant for real power
    computation and angle is constant for reactive power computation so there is no cross coupling
    between V and theta for partial derivatives*/
    void fastDecoupledDeriv();
    /** @brief switch open derivatives*/
    void swOpenDeriv();
    /** @brief load information into the linkInfo structure*/
    void loadLinkInfo();
    /** @brief load information into the linkInfo structure
    @param[in] sD  the state Data
    @param[in] sMode the corresponding solver Mode*/
    void loadLinkInfo(const stateData& sD, const solverMode& sMode);
    /** @brief load the approximation functions in the bizarrely defined array above*/
    void loadApproxFunctions();

    void switchChange(int switchNum) override;
};

}  // namespace griddyn
