/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../gridDynDefinitions.hpp"
#include "core/objectOperatorInterface.hpp"
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
class stateGrabber;
class gridGrabber;
class stateData;
class solverMode;

/** class pairing up basicGrabbers and state grabbers in a single interface
 */
class grabberSet: public objectOperatorInterface {
  private:
    std::shared_ptr<gridGrabber> grab;  //!< the non state grabber
    std::shared_ptr<stateGrabber> stGrab;  //!< the state grabber
    std::unique_ptr<utilities::valuePredictor<coreTime, double, double>>
        predictor;  //!< pointer to a predictor object

  public:
    /** create a grabber from a field String and object
    @param[in] fld the field to grab from an object
    @param[in] obj the object to get the field from
    @param[in] step_only if set to true the underlying stateGrabber is not constructed
    */
    grabberSet(std::string_view fld, coreObject* obj, bool step_only = false);
    /** create a grabber from an offset index
    @param[in] noffset the offset into the state to grab
    @param[in] obj the object to get the field from
    */
    grabberSet(index_t noffset, coreObject* obj);
    /** create a grabber from a gridGrabber and stateGrabber*/
    grabberSet(std::shared_ptr<gridGrabber> ggrab, std::shared_ptr<stateGrabber> stgrab);
    /** destructor*/
    virtual ~grabberSet();

    /** clone function
     *@return a unique_ptr to another GrabberSet*/
    virtual std::unique_ptr<grabberSet> clone() const;
    /** cloneTo function
     *@param[in] gset a pointer to another grabberSet function to clone the data to
     */
    virtual void cloneTo(grabberSet* gset) const;
    /** update the field of grabber
     *@param[in]  fld the new field to capture
     *@throw unrecognized parameter exception if fld is not available
     */
    virtual void updateField(std::string_view fld);
    /** replace the grabbers with a new pair
     */
    virtual void updateGrabbers(std::shared_ptr<gridGrabber> ggrab,
                                std::shared_ptr<stateGrabber> stgrab);

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
    virtual double grabData(const stateData& sD, const solverMode& sMode);
    virtual void outputPartialDerivatives(const stateData& sD,
                                          matrixData<double>& md,
                                          const solverMode& sMode);
    // virtual void getDoutDt(const stateData &sD, const solverMode &sMode) const;
    /** get a description of the grabberSet*/
    virtual const std::string& getDesc() const;
    /** get a description of the grabber Set*/
    virtual std::string getDesc();
    /** set the grabber description*/
    void setDescription(const std::string& newDesc);
    virtual void updateObject(coreObject* obj,
                              object_update_mode mode = object_update_mode::direct) override;
    virtual coreObject* getObject() const override;
    virtual void getObjects(std::vector<coreObject*>& objects) const override;
    /** set the gain of the grabbers*/
    void setGain(double newGain);
    /** check if the grabberSet is using state information*/
    bool stateCapable() const;
    /** check if the grabberSet can compute a Jacobian*/
    bool hasJacobian() const;
};

}  // namespace griddyn
