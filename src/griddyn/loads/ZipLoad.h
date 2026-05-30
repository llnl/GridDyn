/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../Load.h"
#include <string>

namespace griddyn {
/** primary load class supports 3 main types of loads  constant power, constant impedance, constant
current these loads should for the basis of most non dynamic load models following the ZIP model
Z-constant impedance, I-constant current, P- constant Power
*/
class ZipLoad: public GridLoad {
  public:
    enum LoadFlags {
        convert_to_constant_impedance = object_flag2,
        no_pqvoltage_limit = object_flag3,
    };

  private:
    model_parameter Ip = 0.0;  //!< [pu] real current; (constant current)
    model_parameter Iq = 0.0;  //!< [pu] imaginary current (constant current)
    model_parameter Yp = 0.0;  //!< [pu] the impedance load in MW
    model_parameter Yq = 0.0;  //!< [pu]  the reactive impedance load in MVar
  protected:
    double Pout = 0.0;  //!<[puMW] the actual output power
    double Qout = 0.0;  //!<[puMVA] the actual output power
    // double Psched = 0.0;                            //!<[puMW] the scheduled output power

    double Vpqmin = 0.7;  //!< low voltage at which the PQ powers convert to an impedance type load
    double Vpqmax =
        1.3;  //!< upper voltage at which the PQ powers convert to an impedance type load
    CoreTime lastTime = negTime;

  private:
    double trigVVlow =
        1.0 / (0.7 * 0.7);  //!< constant for conversion of PQ loads to constant impedance loads
    double trigVVhigh =
        1.0 / (1.3 * 1.3);  //!< constant for conversion of PQ loads to constant impedance loads
  public:
    explicit ZipLoad(const std::string& objName = "zip_$");
    ZipLoad(double rP, double rQ, const std::string& objName = "zip_$");

    virtual CoreObject* clone(CoreObject* obj = nullptr) const override;
    virtual void pFlowObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void dynObjectInitializeA(CoreTime time0, std::uint32_t flags) override;

    virtual void timestep(CoreTime time, const IOdata& inputs, const SolverMode& sMode) override;
    virtual void getParameterStrings(stringVec& pstr, ParamStringType pstype) const override;

    virtual void set(std::string_view param, std::string_view val) override;
    virtual void
        set(std::string_view param, double val, units::unit unitType = units::defunit) override;
    virtual void setFlag(std::string_view flag, bool val = true) override;

    virtual double get(std::string_view param,
                       units::unit unitType = units::defunit) const override;

    virtual void updateLocalCache(const IOdata& inputs,
                                  const StateData& sD,
                                  const SolverMode& sMode) override;

    virtual void setState(CoreTime time,
                          const double state[],
                          const double dstate_dt[],
                          const SolverMode& sMode) override;

    virtual void ioPartialDerivatives(const IOdata& inputs,
                                      const StateData& sD,
                                      MatrixData<double>& md,
                                      const IOlocs& inputLocs,
                                      const SolverMode& sMode) override;
    virtual void outputPartialDerivatives(const IOdata& inputs,
                                          const StateData& sD,
                                          MatrixData<double>& md,
                                          const SolverMode& sMode) override;
    virtual count_t outputDependencyCount(index_t num, const SolverMode& sMode) const override;

    virtual double getRealPower(const IOdata& inputs,
                                const StateData& sD,
                                const SolverMode& sMode) const override;
    virtual double getReactivePower(const IOdata& inputs,
                                    const StateData& sD,
                                    const SolverMode& sMode) const override;
    virtual double getRealPower(double V) const override;
    virtual double getReactivePower(double V) const override;
    virtual double getRealPower() const override;
    virtual double getReactivePower() const override;  // for saving the state

    friend bool compareLoad(ZipLoad* ld1, ZipLoad* ld2, bool printDiff);

  protected:
    // getters for the actual property values

    double getYp() const { return Yp; }
    double getYq() const { return Yq; }

    double getIp() const { return Ip; }

    double getIq() const { return Iq; }

    double getr() const;

    double getx() const;

    void setup(double newYp);

    void setYq(double newYq);

    void setIp(double newIp);

    void setIq(double newIq);

    void setr(double newr);

    void setx(double newx);

    /** compute the voltage adjustment by min and max voltages*/
    double voltageAdjustment(double val, double voltage) const;
    /** get the Q from power factor*/
    double getQval() const;
};

bool compareLoad(ZipLoad* ld1, ZipLoad* ld2, bool printDiff = false);

}  // namespace griddyn
