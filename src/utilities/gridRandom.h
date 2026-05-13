/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include <memory>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
/** abstract class defining a random distribution*/

namespace utilities {
/** base class for object wrapping a random distribution */
class distributionObject {
  public:
    /**default constructor */
    explicit distributionObject() = default;
    /** virtual default destructor*/
    virtual ~distributionObject() = default;
    /** generate a random value */
    virtual double operator()() = 0;
    /** update a parameter*/
    virtual void updateParameter(double param1) = 0;
    /** update two parameters
    @param param1  the first distribution parameter often the mean
    @param param2 the second random distribution parameter often the std dev*/
    virtual void updateParameters(double param1, double param2)
    {
        (void)(param2);  // ignore it and forward to single parameter call
        updateParameter(param1);
    };
};
// TODO(phlpt): Rework this so it functions across threads as the current generator is not thread
// safe.

/** class defining random number generation*/
class gridRandom {
  private:
    static std::unique_ptr<std::mt19937>
        mGenerator;  //!< generator  //May need to make a generator per thread
    static unsigned int mActualSeed;
    static bool mSeeded;

  public:
    /** set the seed of the random number generator*/
    static void setSeed(unsigned int seed);
    /** automatically generate a seed*/
    static void setSeed();
    /** get the current seed*/
    static unsigned int getSeed();
    /** enumeration of the different distribution types*/
    enum class DistributionType {
        CONSTANT,
        UNIFORM,
        EXPONENTIAL,
        NORMAL,
        LOGNORMAL,
        EXTREME_VALUE,
        GAMMA,
        UNIFORM_INT,
    };
    explicit gridRandom(DistributionType dist = DistributionType::NORMAL,
                        double param1 = 0.0,
                        double param2 = 1.0);
    explicit gridRandom(std::string_view dist_name, double param1 = 0.0, double param2 = 1.0);

    void setDistribution(DistributionType dist);
    DistributionType getDistribution() const { return mDist; }
    /** generate a random number according to the distribution*/
    double operator()();
    /** generate a random number according to the distribution*/
    double generate();
    /** set the parameters of the distribution*/
    void setParameters(double param1, double param2 = 1.0);
    /** generate a random number from a distribution*/
    static double randNumber(DistributionType dist);
    /** generate a random number from a distribution with a two parameter distribution*/
    static double randNumber(DistributionType dist, double param1, double param2);
    /** generate a pair of random numbers*/
    std::pair<double, double> getPair();
    /** generate a random vector with a specified number of values*/
    std::vector<double> getNewValues(size_t count);
    /** fill a vector with new random values
    @param[out] rvec the vector to fill with new random numbers
    @param count the number of random values to generate
    */
    void getNewValues(std::vector<double>& rvec, size_t count);

    static auto& getEngine() { return *mGenerator; }

  private:
    static void ensureEngine();
    std::unique_ptr<distributionObject> mDistribution;
    DistributionType mDist;
    double mParam1 = 0.0;
    double mParam2 = 1.0;
};

/** class describing a random distribution which takes two parameters
 */
template<class DIST>
class randomDistributionObject2: public distributionObject {
  private:
    DIST dist;  //!< the actual distribution object

  public:
    randomDistributionObject2() {}
    explicit randomDistributionObject2(double param1): dist(param1) {}
    randomDistributionObject2(double param1, double param2): dist(param1, param2) {}
    virtual double operator()() override { return dist(gridRandom::getEngine()); }
    virtual void updateParameter(double param1) override { dist = DIST(param1); }
    virtual void updateParameters(double param1, double param2) override
    {
        dist = DIST(param1, param2);
    }
};

/** template class describing a random distribution which takes 1 parameter*/
template<class DIST>
class randomDistributionObject1: public distributionObject {
  private:
    DIST dist;  //!< the actual distribution object

  public:
    randomDistributionObject1() {}
    explicit randomDistributionObject1(double param1): dist(param1) {}
    virtual double operator()() override { return dist(gridRandom::getEngine()); }
    virtual void updateParameter(double param1) override { dist = DIST(param1); }
};

/** a template specialization for a making a constant look like a random distribution*/
template<>
class randomDistributionObject1<void>: public distributionObject {
  private:
    double mParam1 = 0.0;  //!< the constant value to generate

  public:
    randomDistributionObject1() {}
    explicit randomDistributionObject1(double param1): mParam1(param1) {}
    virtual double operator()() override { return mParam1; }
    virtual void updateParameter(double param1) override { mParam1 = param1; }
};

/** get the distribution type from a string*/
gridRandom::DistributionType getDist(std::string_view dist_name);

}  // namespace utilities
