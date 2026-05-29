/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"
#include "core/ObjectOperatorInterface.hpp"
#include <memory>
#include <string>
#include <string_view>

template<class Y>
class matrixData;

namespace utilities {
template<typename X, typename Y, typename Z>
class valuePredictor;
}

namespace griddyn {
class gridCore;
class StateGrabber;
class GridGrabber;
class StateData;
class SolverMode;

/** class pairing up basicGrabbers and state grabbers in a single interface
 */
class GrabberSet: public ObjectOperatorInterface {
  private:
    std::shared_ptr<GridGrabber> mGrabber;  //!< the non state grabber
    std::shared_ptr<StateGrabber> mStateGrabber;  //!< the state grabber
    std::unique_ptr<utilities::valuePredictor<coreTime, double, double>>
        mPredictor;  //!< pointer to a predictor object

  public:
    /** create a grabber from a field String and object
    @param[in] fld the field to grab from an object
    @param[in] obj the object to get the field from
    @param[in] step_only if set to true the underlying StateGrabber is not constructed
    */
    GrabberSet(std::string_view fld, CoreObject* obj, bool step_only = false);
    /** create a grabber from an offset index
    @param[in] noffset the offset into the state to grab
    @param[in] obj the object to get the field from
    */
    GrabberSet(index_t noffset, CoreObject* obj);
    /** create a grabber from a GridGrabber and StateGrabber*/
    GrabberSet(std::shared_ptr<GridGrabber> ggrab, std::shared_ptr<StateGrabber> stgrab);
    /** destructor*/
    virtual ~GrabberSet();

    /** clone function
     *@return a unique_ptr to another GrabberSet*/
    virtual std::unique_ptr<GrabberSet> clone() const;
    /** cloneTo function
     *@param[in] gset a pointer to another GrabberSet function to clone the data to
     */
    virtual void cloneTo(GrabberSet* gset) const;
    /** update the field of grabber
     *@param[in]  fld the new field to capture
     *@throw unrecognized parameter exception if fld is not available
     */
    virtual void updateField(std::string_view fld);
    /** replace the grabbers with a new pair
     */
    virtual void updateGrabbers(std::shared_ptr<GridGrabber> ggrab,
                                std::shared_ptr<StateGrabber> stgrab);

    /** actually go and get the data
     *@return the value produced by the grabber*/
    virtual double grabData();
    /** @brief grab a vector of data
     *@param[out] data the vector to store the data in
     */
    virtual void grabData(std::vector<double>& data);
    /** @brief get the descriptions of the data
     *@param[out] desc_list  the list of descriptions
     **/
    virtual void getDesc(std::vector<std::string>& desc_list) const;
    virtual double grabData(const StateData& stateDataValue, const SolverMode& sMode);
    virtual void outputPartialDerivatives(const StateData& stateDataValue,
                                          matrixData<double>& matrixDataValue,
                                          const SolverMode& sMode);
    // virtual void getDoutDt(const StateData&sD, const SolverMode &sMode) const;
    /** get a description of the GrabberSet*/
    virtual const std::string& getDesc() const;
    /** get a description of the grabber Set*/
    virtual std::string getDesc();
    /** set the grabber description*/
    void setDescription(const std::string& newDesc);
    virtual void updateObject(CoreObject* obj,
                              ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override;
    virtual CoreObject* getObject() const override;
    virtual void getObjects(std::vector<CoreObject*>& objects) const override;
    /** set the gain of the grabbers*/
    void setGain(double newGain);
    /** check if the GrabberSet is using state information*/
    bool stateCapable() const;
    /** check if the GrabberSet can compute a Jacobian*/
    bool hasJacobian() const;
};

}  // namespace griddyn
