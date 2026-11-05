/*
 * Copyright (c) 2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "core/CoreExceptions.h"
#include "core/ModelDescription.h"
#include "griddyn/GenModel.h"
#include "griddyn/Stabilizer.h"
#include "griddyn/genmodels/GenModelCSVGN1.h"
#include "griddyn/genmodels/otherGenModels.h"
#include "griddyn/stabilizers/StabilizerIEEEST.h"
#include "griddyn/stabilizers/StabilizerIee2st.h"
#include "griddyn/stabilizers/StabilizerPss2a.h"
#include "griddyn/stabilizers/StabilizerPss2b.h"
#include "griddyn/stabilizers/StabilizerST2CUT.h"
#include "griddyn/stabilizers/StabilizerStab2a.h"
#include "griddyn/stabilizers/StabilizerStab3.h"
#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <vector>

using namespace griddyn;

TEST(MachineDescriptionTests, DescribesEveryNativeGeneratorAndStabilizerType)
{
    std::vector<std::unique_ptr<CoreObject>> models;
    models.emplace_back(std::make_unique<GenModel>());
    models.emplace_back(std::make_unique<genmodels::GenModelClassical>());
    models.emplace_back(std::make_unique<genmodels::GenModel3>());
    models.emplace_back(std::make_unique<genmodels::GenModel4>());
    models.emplace_back(std::make_unique<genmodels::GenModel5>());
    models.emplace_back(std::make_unique<genmodels::GenModel5type2>());
    models.emplace_back(std::make_unique<genmodels::GenModel5type3>());
    models.emplace_back(std::make_unique<genmodels::GenModel6>());
    models.emplace_back(std::make_unique<genmodels::GenModel6type2>());
    models.emplace_back(std::make_unique<genmodels::GenModel8>());
    models.emplace_back(std::make_unique<genmodels::GenModelGENROU>());
    models.emplace_back(std::make_unique<genmodels::GenModelGENROE>());
    models.emplace_back(std::make_unique<genmodels::GenModelGENSAL>());
    models.emplace_back(std::make_unique<genmodels::GenModelGENSAE>());
    models.emplace_back(std::make_unique<genmodels::GenModelGENTPJ>());
    models.emplace_back(std::make_unique<genmodels::GenModelInverter>());
    models.emplace_back(std::make_unique<genmodels::GenModelCSVGN1>());
    models.emplace_back(std::make_unique<Stabilizer>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerIEEEST>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerIee2st>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerST2CUT>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerPss2a>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerPss2b>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerStab2a>());
    models.emplace_back(std::make_unique<stabilizers::StabilizerStab3>());
    for (const auto& model : models) {
        SCOPED_TRACE(model->getModelDescription().name());
        EXPECT_TRUE(model->getModelDescription().describesExactType(*model));
        const auto saved = ModelFieldSnapshot::capture(*model, FieldSnapshotLevel::PARAMETERS);
        EXPECT_NO_THROW(saved.restore(*model));
    }
}

TEST(MachineDescriptionTests, GeneratorAliasesUnitsAndCoupledSetters)
{
    genmodels::GenModelGENROU model;
    model.set("td0p", 500.0, units::ms);
    EXPECT_NEAR(model.get("tdop"), 0.5, 1e-6);
    EXPECT_NEAR(model.get("td0p", units::ms), 500.0, 1e-4);
    model.set("x", 1.8);
    EXPECT_DOUBLE_EQ(model.get("xd"), 1.8);
    EXPECT_DOUBLE_EQ(model.get("xq"), 1.8);
    model.set("xpp", 0.2);
    EXPECT_DOUBLE_EQ(model.get("xdpp"), 0.2);
    EXPECT_DOUBLE_EQ(model.get("xqpp"), 0.2);
    EXPECT_DOUBLE_EQ(model.get("xpp"), 0.2);
    model.set("top", 2.0);
    EXPECT_DOUBLE_EQ(model.get("tdop"), 2.0);
    EXPECT_DOUBLE_EQ(model.get("tqop"), 2.0);
    model.set("mbase", 200000.0, units::kW);
    EXPECT_NEAR(model.get("base"), 200.0, 1e-5);
    const auto previous = model.get("tdop");
    EXPECT_THROW(model.set("tdop", 1.0, units::MW), InvalidParameterValue);
    EXPECT_DOUBLE_EQ(model.get("tdop"), previous);
    EXPECT_NO_THROW(model.set("#comment", std::numeric_limits<double>::quiet_NaN()));
}

TEST(MachineDescriptionTests, StabilizerSelectorsLimitsAndSubclassValidation)
{
    stabilizers::StabilizerPss2a pss2a;
    stabilizers::StabilizerPss2b pss2b;
    EXPECT_THROW(pss2a.set("t9", 0.0), InvalidParameterValue);
    EXPECT_NO_THROW(pss2b.set("t9", 0.0));
    EXPECT_THROW(pss2b.set("m", 1.5), InvalidParameterValue);
    EXPECT_THROW(pss2b.set("mode1", 6.0), InvalidParameterValue);
    EXPECT_THROW(pss2b.set("busr1", 1.0), InvalidParameterValue);
    pss2b.set("tw1", 250.0, units::ms);
    EXPECT_NEAR(pss2b.get("tw1"), 0.25, 1e-6);
    pss2b.set("vstmax", 20.0, units::percent);
    EXPECT_NEAR(pss2b.get("vmax"), 0.2, 1e-6);
    EXPECT_THROW(pss2b.set("vmin", 0.3), InvalidParameterValue);
    EXPECT_NEAR(pss2b.get("vmin"), -0.1, 1e-6);

    stabilizers::StabilizerIEEEST ieeest;
    ieeest.set("a2", 40000.0, units::ms * units::ms);
    EXPECT_NEAR(ieeest.get("a2"), 0.04, 1e-6);
    EXPECT_THROW(ieeest.set("mode", 2.0), InvalidParameterValue);
    ieeest.set("vcu", 0.0);
    EXPECT_DOUBLE_EQ(ieeest.get("vcu"), 999.0);
    EXPECT_NO_THROW(ieeest.set("", std::numeric_limits<double>::quiet_NaN()));
}
