/*
 * LLNS Copyright Start
 * Copyright (c) 2014-2018, Lawrence Livermore National Security
 * This work was performed under the auspices of the U.S. Department
 * of Energy by Lawrence Livermore National Laboratory in part under
 * Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
 * Produced at the Lawrence Livermore National Laboratory.
 * All rights reserved.
 * For details, see the L */

#ifndef VALUE_PREDICTOR_H_
#define VALUE_PREDICTOR_H_
#pragma once

namespace utilities {
/** @brief class implementing a prediction system
 *@details the base class does a linear prediction
 */
template<typename InputType, typename OutputType = InputType, typename SlopeType = OutputType>
class valuePredictor {
  private:
    InputType lastKnownInput_;  //!< the last known time
    OutputType lastKnownOutput_;  //!< the last known Value
    SlopeType slope_;  //!< the rate of change

  public:
    /** construction the predictor
    * @param[in] input0 the initial input
    @param[in] output0 the initial output
    @param[in] slope0 [optional] the initial rate of change
    */
    valuePredictor(InputType input0, OutputType output0, SlopeType slope0 = SlopeType(0)):
        lastKnownInput_(input0), lastKnownOutput_(output0), slope_(slope0)
    {
    }
    /** destructor*/
    virtual ~valuePredictor() = default;
    // default copy and copy constructors
    valuePredictor(const valuePredictor& ref) =
        default;  //!< copy constructor does the default thing
    /** move constructor*/
    valuePredictor(valuePredictor&& ref) = default;
    /** copy operator*/
    valuePredictor& operator=(const valuePredictor& ref) = default;
    /** move operator*/
    valuePredictor& operator=(valuePredictor&& ref) = default;
    /** update the known values
     *@details sets the known values and computes the ramp rate
     *The values input should correspond to the reduction in values so 0.0 for no saturation
     *@param[in] input  the actual input value
     *@param[in] output the actual output value
     */
    virtual void update(InputType input, OutputType output)
    {
        if (input - lastKnownInput_ > InputType{0}) {
            slope_ = (output - lastKnownOutput_) / (input - lastKnownInput_);
        }
        lastKnownInput_ = input;
        lastKnownOutput_ = output;
    }
    /** @brief set the rate at a user specified value
     */
    virtual void setSlope(SlopeType newSlope) { slope_ = newSlope; }
    /** update the saturation type function by enumeration*/
    virtual OutputType predict(InputType input) const
    {
        return lastKnownOutput_ + (input - lastKnownInput_) * slope_;
    }
    /** update the saturation type function by enumeration*/
    OutputType operator()(InputType input) const { return predict(input); }
    /** getKnownInput*/
    InputType getKnownInput() const { return lastKnownInput_; }
    /** getKnownOutput*/
    OutputType getKnownOutput() const { return lastKnownOutput_; }
    /** get the rate of change*/
    SlopeType getSlope() const { return slope_; }
};
}  // namespace utilities
#endif
