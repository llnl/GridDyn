/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_EQUATIONS_EQUATION_ODE_H_
#define ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_EQUATIONS_EQUATION_ODE_H_

#include "Equation.h"

namespace griddyn {
namespace paradae {

    class Equation_ODE: public Equation {
      public:
        // Equation_ODE specific
        virtual void
            function_ode(const Real t, const Vector& y, const Vector& state, Vector& dy) = 0;
        virtual void jacobian_ode(const Real t,
                                  const Vector& y,
                                  const Vector& state,
                                  Matrix& Jy,
                                  bool add = false);
        void approx_jacobian_ode(const Real t,
                                 const Vector& y,
                                 const Vector& state,
                                 Matrix& J,
                                 bool add = false);
        void approx_jacobian_ode(const Real t,
                                 const Vector& y,
                                 const Vector& fy,
                                 const Vector& state,
                                 Matrix& J,
                                 bool add = false);

        // Redefinition of inherited virtual methods
        virtual ~Equation_ODE() {};
        virtual void function(const Real t,
                              const Vector& y,
                              const Vector& dy,
                              const Vector& state,
                              Vector& Fydy);
        virtual void jacobian_y(const Real t,
                                const Vector& y,
                                const Vector& dy,
                                const Vector& state,
                                Matrix& J,
                                bool add = false);
        virtual void jacobian_dy(const Real t,
                                 const Vector& y,
                                 const Vector& dy,
                                 const Vector& state,
                                 Matrix& J,
                                 bool add = false);
        virtual void Get_dy_from_y(const Real t, const Vector& y, const Vector& state, Vector& dy)
        {
            function_ode(t, y, state, dy);
        };
        virtual type_Equation GetTypeEq() { return ODE; };
    };
}  // namespace paradae
}  // namespace griddyn

#endif  // ___W_GRIDDYN_GRIDDYN_SRC_EXTRASOLVERS_PARADAE_EQUATIONS_EQUATION_ODE_H_
