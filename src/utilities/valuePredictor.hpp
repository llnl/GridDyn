/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

namespace utilities {
/** @brief class implementing a prediction system
 *@details the base class does a linear prediction
 */
template<typename InputType, typename OutputType = InputType, typename SlopeType = OutputType>
class valuePredictor {
  private:
    InputType mLastKnownInput;  //!< the last known time
    OutputType mLastKnownOutput;  //!< the last known Value
    SlopeType mSlope;  //!< the rate of change

  public:
    /** construction the predictor
    * @param[in] input0 the initial input
    @param[in] output0 the initial output
    @param[in] slope0 [optional] the initial rate of change
    */
    valuePredictor(InputType input0, OutputType output0, SlopeType slope0 = SlopeType(0)):
        mLastKnownInput(input0), mLastKnownOutput(output0), mSlope(slope0)
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
        if (input - mLastKnownInput > InputType{0}) {
            mSlope = (output - mLastKnownOutput) / (input - mLastKnownInput);
        }
        mLastKnownInput = input;
        mLastKnownOutput = output;
    }
    /** @brief set the rate at a user specified value
     */
    virtual void setSlope(SlopeType newSlope) { mSlope = newSlope; }
    /** update the saturation type function by enumeration*/
    virtual OutputType predict(InputType input) const
    {
        return mLastKnownOutput + (input - mLastKnownInput) * mSlope;
    }
    /** update the saturation type function by enumeration*/
    OutputType operator()(InputType input) const { return predict(input); }
    /** getKnownInput*/
    InputType getKnownInput() const { return mLastKnownInput; }
    /** getKnownOutput*/
    OutputType getKnownOutput() const { return mLastKnownOutput; }
    /** get the rate of change*/
    SlopeType getSlope() const { return mSlope; }
};
}  // namespace utilities
