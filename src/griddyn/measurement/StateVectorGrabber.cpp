/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

/** @file
 * @brief Build solver-state field vectors using existing scalar state readers.
 *
 * Implements createStateGrabber() for area/simulation vectors. The implementation
 * is private so callers can keep using the StateGrabber interface and factory.
 */

#include "../GridArea.h"
#include "../GridBus.h"
#include "../Link.h"
#include "GrabberUnitConversion.hpp"
#include "ObjectGrabbers.h"
#include "StateGrabber.h"
#include "utilities/MatrixDataScale.hpp"
#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace griddyn {
namespace {
    /** @brief Map each supported area vector to its corresponding scalar element field. */
    const std::map<std::string_view, std::string_view> ELEMENT_FIELDS{{"voltage", "voltage"},
                                                                      {"angle", "angle"},
                                                                      {"freq", "freq"},
                                                                      {"busfreq", "freq"},
                                                                      {"busgenreal", "gen"},
                                                                      {"busgenreactive",
                                                                       "genreactive"},
                                                                      {"busloadreal", "loadreal"},
                                                                      {"busloadreactive",
                                                                       "loadreactive"},
                                                                      {"linkreal", "p"},
                                                                      {"linkreactive", "q"},
                                                                      {"linkloss", "loss"}};

    /** @brief Append vector members in the same order as object-side area vectors.
     * @param area Area whose hierarchy is traversed; must be non-null.
     * @param links Select links when true, otherwise buses.
     * @param[out] objects Members appended after any existing entries.
     *
     * Visits nested areas before direct children, preserving each stored order.
     * Membership is not filtered by enabled/connected status. Indexed object
     * lookup can use a different ordering; do not substitute it here.
     */
    // NOLINTNEXTLINE(misc-no-recursion)
    void gatherMembers(GridArea* area, bool links, std::vector<CoreObject*>& objects)
    {
        for (index_t index = 0; area->getArea(index) != nullptr; ++index) {
            auto* child = area->getArea(index);
            gatherMembers(child, links, objects);
        }
        if (links) {
            for (index_t index = 0; area->getLink(index) != nullptr; ++index) {
                auto* link = area->getLink(index);
                objects.push_back(link);
            }
        } else {
            for (index_t index = 0; area->getBus(index) != nullptr; ++index) {
                auto* bus = area->getBus(index);
                objects.push_back(bus);
            }
        }
    }

    /** @brief Assemble an area field vector from scalar solver-state grabbers.
     *
     * Reuses scalar value/Jacobian callbacks instead of implementing a second
     * set of bus/link physics. Field aliases come from the object registry and
     * ordering matches its area vectors. Every element must load successfully;
     * Jacobians are advertised only when every element supplies them.
     *
     * Membership is bound during field selection or retargeting. After topology
     * changes, rebuild or retarget the selection and recheck/reset its collector
     * layout. Reads use the supplied StateData wherever each scalar field
     * supports it; cached summary fields do not gain derivatives automatically.
     * The target objects must outlive the grabber. This reader is distinct from
     * the object-only all_state/all_model expansion in GrabberInterpreter.cpp.
     * @see createStateGrabber, StateGrabber
     */
    class StateFieldVectorGrabber final: public StateGrabber {
        std::vector<std::unique_ptr<StateGrabber>> mElements;  //!< Owned readers in output order.

        /** @brief Combine this vector's gain and affine unit-conversion Jacobian scale. */
        [[nodiscard]] double derivativeScale() const
        {
            if (outputUnits == units::defunit) {
                return gain;
            }
            const GrabberUnitConversion conversion(inputUnits, outputUnits, cobj);
            return gain * conversion.affineScale();
        }

      public:
        /** @brief Select a supported vector field and bind its area members.
         * @param selection Field or alias understood by the object vector registry.
         * @param area Area or simulation supplying the vector's members.
         */
        StateFieldVectorGrabber(std::string_view selection, GridArea* area)
        {
            field = selection;
            updateObject(area);
        }

        /** @brief Retarget directly or by matching association, then rebuild membership. */
        void updateObject(CoreObject* object,
                          ObjectUpdateMode mode = ObjectUpdateMode::DIRECT) override
        {
            if (mode == ObjectUpdateMode::MATCH) {
                object = findMatchingObject(cobj, object);
            }
            cobj = dynamic_cast<GridComponent*>(object);
            updateField(std::string{field});
        }

        /** @brief Rebind scalar element readers and recompute loaded/Jacobian status.
         * @param selection New field name or alias.
         * @note Unsupported vector selections fall back to ordinary scalar field loading.
         */
        void updateField(std::string_view selection) override
        {
            mElements.clear();
            auto* area = dynamic_cast<GridArea*>(cobj);
            const auto canonical = canonicalGrabberField(selection);
            const auto found = ELEMENT_FIELDS.find(canonical);
            if (area == nullptr || found == ELEMENT_FIELDS.end() ||
                !getObjectVectorFunction(area, selection).first) {
                StateGrabber::updateField(selection);
                return;
            }
            field = selection;
            inputUnits = getObjectVectorFunction(area, selection).second;
            vectorGrab = true;
            loaded = true;
            jacMode = JacobianMode::COMPUTED;
            std::vector<CoreObject*> objects;
            gatherMembers(area, canonical.starts_with("link"), objects);
            for (auto* element : objects) {
                auto grabber = std::make_unique<StateGrabber>(found->second, element);
                loaded = loaded && grabber->loaded;
                if (grabber->getJacobianMode() == JacobianMode::NONE) {
                    jacMode = JacobianMode::NONE;
                }
                inputUnits = grabber->inputUnits;
                mElements.push_back(std::move(grabber));
            }
        }

        /** @brief Deep-copy bound element readers and configuration, retaining their targets. */
        [[nodiscard]] std::unique_ptr<StateGrabber> clone() const override
        {
            auto result =
                std::make_unique<StateFieldVectorGrabber>(field, dynamic_cast<GridArea*>(cobj));
            cloneTo(result.get());
            return result;
        }

        /** @brief Copy configuration and, for the matching type, replace its element readers.
         * @param destination Non-null destination; self-copy is a no-op.
         */
        void cloneTo(StateGrabber* destination) const override
        {
            if (destination == this) {
                return;
            }
            StateGrabber::cloneTo(destination);
            if (auto* vector = dynamic_cast<StateFieldVectorGrabber*>(destination)) {
                vector->mElements.clear();
                for (const auto& element : mElements) {
                    vector->mElements.push_back(element->clone());
                }
            }
        }

        /** @brief Replace values with bound element samples, then convert and scale each.
         * @param data Solver state passed to each scalar reader.
         * @param mode Solver layout associated with @p data.
         * @param[out] values Cleared first; remains empty for unloaded/nonvector selections.
         * @note Unit conversion precedes gain and bias. Reads do not refresh membership.
         */
        void grabVectorData(const StateData& data,
                            const SolverMode& mode,
                            std::vector<double>& values) override
        {
            values.clear();
            if (!loaded || !vectorGrab) {
                return;
            }
            const GrabberUnitConversion conversion(inputUnits, outputUnits, cobj);
            values.reserve(mElements.size());
            for (const auto& element : mElements) {
                const double value = element->grabData(data, mode);
                values.push_back(std::fma(
                    outputUnits == units::defunit ? value : conversion.convert(value), gain, bias));
            }
        }

        /** @brief Add one element's partials, including conversion and gain, to row zero.
         * @param data Solver state passed to the element reader.
         * @param matrix Additive destination; existing entries are not cleared.
         * @param mode Solver layout associated with @p data.
         * @param element Zero-based vector element. Out-of-range elements add nothing.
         * @note Nonvector selections delegate to the base scalar behavior.
         */
        void outputVectorPartialDerivatives(const StateData& data,
                                            MatrixData<double>& matrix,
                                            const SolverMode& mode,
                                            size_t element) override
        {
            if (!vectorGrab) {
                StateGrabber::outputVectorPartialDerivatives(data, matrix, mode, element);
            } else if (loaded && element < mElements.size()) {
                MatrixDataScale<double> scaled(matrix, derivativeScale());
                mElements[element]->outputPartialDerivatives(data, scaled, mode);
            }
        }

        /** @brief Replace column labels using each bound object's name and selected field. */
        void getDesc(stringVec& descriptions) const override
        {
            if (!vectorGrab) {
                StateGrabber::getDesc(descriptions);
                return;
            }
            descriptions.clear();
            for (const auto& element : mElements) {
                descriptions.push_back(element->getObject()->getName() + ':' + field);
            }
        }

        /** @brief Append all bound element targets without clearing or deduplicating the output. */
        void getObjects(std::vector<CoreObject*>& objects) const override
        {
            if (!vectorGrab) {
                StateGrabber::getObjects(objects);
                return;
            }
            for (const auto& element : mElements) {
                element->getObjects(objects);
            }
        }
    };
}  // namespace

std::unique_ptr<StateGrabber> createStateGrabber(std::string_view field, CoreObject* object)
{
    auto* area = dynamic_cast<GridArea*>(object);
    if (area != nullptr) {
        // Only fields that are vectors in the object registry become state vectors.
        const auto vectorFunction = getObjectVectorFunction(area, field);
        if (vectorFunction.first) {
            const auto canonical = canonicalGrabberField(field);
            const auto found = ELEMENT_FIELDS.find(canonical);
            if (found != ELEMENT_FIELDS.end()) {
                return std::make_unique<StateFieldVectorGrabber>(field, area);
            }
        }
    }
    return std::make_unique<StateGrabber>(field, object);
}
}  // namespace griddyn
