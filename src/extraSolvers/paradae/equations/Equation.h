/*
 * Copyright (c) 2014-2026, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#pragma once

#include "../common/def.h"
#include "../math/IPoly.h"
#include "../math/SVector.h"
#include "../math/Vector.h"
#include "../math/VirtualMatrix.h"
#include <string>

namespace griddyn::paradae {

    enum type_Equation { ODE, DAE };

    class RootManager {
      public:
        int n_sroots;  // Number of scheduled roots
        int n_uroots;  // Number of unscheduled roots
        int n_uactive;  // Number of active unscheduled roots
        int n_sactive;  // Number of active scheduled roots
                        // If a root is inactive, the code ignores it
        int n_state;  // Size of the state vector, i.e. number of DOFs in space

        SVector is_active;  // Vector of size n_sroots+n_uroots, that says if a root
                            // is active or not.  First n_sroots indices are for
                            // scheduled roots, with the following indices
                            // indicating activity for unscheduled roots

        SVector dir_root;  // Direction of the crossing that would create a discontinuity
                           //   -1: zero is crossed with g decreasing,
                           //    1: zero is crossed with g increasing,
                           //    0: both direction would create a discontinuity

        SVector t_sroot;  // The times of the scheduled discontinuities
        Real tol;  // Root finding tolerance

        RootManager():
            n_sroots(0), n_uroots(0), n_sactive(0), n_uactive(0), n_state(0), is_active(0),
            dir_root(0), t_sroot(0), tol(0) {};
        RootManager(int n_sr, int n_ur, int n_st, Real tol_):
            n_sroots(n_sr), n_uroots(n_ur), n_sactive(0), n_uactive(0), n_state(n_st),
            is_active(n_sr + n_ur), dir_root(n_ur), t_sroot(n_sr), tol(tol_) {};
        inline bool HasSRoots() const { return n_sactive > 0; };
        inline bool HasURoots() const { return n_uactive > 0; };
        inline bool HasRoots() const { return (n_sactive > 0) || (n_uactive > 0); };
        inline int GetNSActRoots() const { return n_sactive; };
        inline int GetNUActRoots() const { return n_uactive; };
        inline int GetNActRoots() { return n_sactive + n_uactive; };
        inline int GetNSRoots() const { return n_sroots; };
        inline int GetNURoots() const { return n_uroots; };
        inline int GetNRoots() { return n_sroots + n_uroots; };
    };

    class Equation {
      protected:
        int n;
        int N_unistep;
        int nb_calls;
        int nb_calls_root;
        int nb_calls_jac;
        Real t0, Tmax;
        std::string name;
        SVector y0;
        RootManager roots;

      public:
        Equation();
        virtual ~Equation() {};
        virtual void function(const Real t,
                              const Vector& y,
                              const Vector& dy,
                              const Vector& state,
                              Vector& Fydy) = 0;
        virtual void jacobian_ypcdy(const Real t,
                                    const Vector& y,
                                    const Vector& dy,
                                    const Vector& state,
                                    const Real cj,
                                    Matrix& J);
        virtual void jacobian_y(const Real t,
                                const Vector& y,
                                const Vector& dy,
                                const Vector& state,
                                Matrix& Jy,
                                bool add = false);
        virtual void jacobian_dy(const Real t,
                                 const Vector& y,
                                 const Vector& dy,
                                 const Vector& state,
                                 Matrix& Jdy,
                                 bool add = false);
        virtual void
            Get_dy_from_y(const Real t, const Vector& y, const Vector& state, Vector& dy) = 0;
        virtual void init(const Real t, Vector& y) = 0;
        virtual type_Equation GetTypeEq() = 0;
        virtual std::string GetName() { return name; };
        // The root functions, implements g(x)=0
        virtual void root_functions(const Real t,
                                    const Vector& y,
                                    const Vector& dy,
                                    const Vector& state,
                                    Vector& rv) {};
        // Change the state depending on which root is found (should be private?)
        virtual void root_crossings(const Vector& iroot, Vector& state) {};
        // initialize the state
        virtual void root_init_state(const Real t, Vector& state) {};
        // Check if a root has been crossed. If so, estimate the time t and change the state.
        bool CheckAllRoots(IPoly& P, Real tlo, Vector& glo, Real& thi, Vector& ghi, Vector& state);
        // Check if a unscheduled root has been crossed. If so, estimate the time t.
        bool CheckUnscheduledRoots(IPoly& P,
                                   Real tlo,
                                   Vector& glo,
                                   Real& thi,
                                   Vector& ghi,
                                   Vector& iroot,
                                   Vector& state);
        // Check if a scheduled root has been crossed. If so, find the time t.
        bool CheckScheduledRoots(Real tlo, Real& thi, int& idx);

        void approx_jacobian_y(const Real t,
                               const Vector& y,
                               const Vector& dy,
                               const Vector& state,
                               Matrix& Jy,
                               bool add = false);
        void approx_jacobian_y(const Real t,
                               const Vector& y,
                               const Vector& dy,
                               const Vector& Fydy,
                               const Vector& state,
                               Matrix& Jy,
                               bool add = false);
        void approx_jacobian_dy(const Real t,
                                const Vector& y,
                                const Vector& dy,
                                const Vector& state,
                                Matrix& Jy,
                                bool add = false);
        void approx_jacobian_dy(const Real t,
                                const Vector& y,
                                const Vector& dy,
                                const Vector& Fydy,
                                const Vector& state,
                                Matrix& Jy,
                                bool add = false);

        inline int GetM() const { return n; };
        inline void SetT0(Real t0_) { t0 = t0_; };
        inline void SetTmax(Real Tmax_) { Tmax = Tmax_; };
        inline void SetNsteps(int N_unistep_) { N_unistep = N_unistep_; };
        inline Real GetT0() { return t0; };
        inline Real GetTmax() { return Tmax; };
        inline int GetNsteps() { return N_unistep; };
        inline int GetNbFunCalls() { return nb_calls; };
        inline int GetNbJacCalls() { return nb_calls_jac; };
        inline int GetNbRootCalls() { return nb_calls_root; };
        inline bool HasEvents() { return roots.HasRoots(); };
        inline bool HasSEvents() { return roots.HasSRoots(); };
        inline bool HasUEvents() { return roots.HasURoots(); };
        inline int GetNRoots() { return roots.GetNRoots(); };
        inline int GetNURoots() { return roots.GetNURoots(); };
        inline int GetNSRoots() { return roots.GetNSRoots(); };
        virtual int GetNState() { return 0; };
    };
}  // namespace griddyn::paradae
