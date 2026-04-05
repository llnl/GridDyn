/*
 * Copyright (c) 2018-2020, Lawrence Livermore National Security
 * See the top-level NOTICE for additional details. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef TimeIntegrator_h
#define TimeIntegrator_h

#include "../common/def.h"
#include "../equations/Equation.h"
#include "../math/PVector.h"
#include "../math/SMultiVector.h"
#include "../solvers/Newton.h"
#include <list>

namespace griddyn {
namespace paradae {

    enum TI_type { RK, BDF };

    enum RCODE { OK, OK_ROOT, WARN_ROOT, ERRTEST_FAIL, NONLIN_FAIL };

    struct DATA_Struct {
        Real t;  // t=tprev(0);
        /* Values at previous points. Should not be changed.*/
        SMultiVector tprev;
        SMultiVector xprev;
        SVector dxprev, gprev, sprev;

        /* Attempted dt. Should not be changed unless all the following conditions are met:
       - a root was on the way.
       - we do variable time steps.
       - we are in sequential mode.
    */
        Real used_dt;

        /* Values at the root, when one is found inside ]t,t+used_dt[ */
        Real troot;
        SVector xroot;
        SVector dxroot, groot, sroot;

        /* Values calculated at next point. We insure consistency of:
       - tn=t+used_dt
       - xnext=x(tn)
       - dxnext=dx(tn)
       - gnext=g(tn)
       - snext=state(tn)
     */
        SVector xnext;
        SVector dxnext, gnext, snext;

        /* Suggested dt
       - for next step, if we had a successful step
       - to redo the step, if the step failed because of the non linear solver or error estimation
     */
        Real next_dt;

        DATA_Struct(int size_x, int size_b, int size_g, int size_s);
        void Rotate(RCODE rc = OK);
        void RollBack();
        void SetNextAtRoot();
    };

    class TimeIntegrator {
      protected:
        int size_x;
        int nb_steps;
        int order;
        int nb_steps_done;
        Real rtol;
        SMultiVector atol;
        Real max_rfactor;

        Equation* equation;
        PVector pstate;

        bool do_braid;
        bool do_varstep;
        bool do_falsevarstep;
        bool dense_mat;

        Newton newton;

      public:
        TimeIntegrator()
        {
            do_braid = false;
            do_varstep = false;
            do_falsevarstep = false;
            dense_mat = true;
            nb_steps_done = 0;
        };
        virtual ~TimeIntegrator() {};
        virtual RCODE AdvanceStep(DATA_Struct& val, int iter_ref = 0) = 0;
        inline int GetSizeX() const { return size_x; };
        inline int GetNbSteps() const { return nb_steps; };
        inline void SetBraid(bool do_braid_ = true) { do_braid = do_braid_; };
        virtual void SetDenseMatrix(bool dense_mat_ = true) { dense_mat = dense_mat_; };
        void SetNewtonSolverOpt(int max_iter, Real tol_, bool update)
        {
            newton = Newton(max_iter, tol_, update);
        };
        void CopyNewtonSolverOpt(const TimeIntegrator& ti) { newton = ti.newton; };
        bool CheckRoots(DATA_Struct& val);
        RCODE PostStep(DATA_Struct& val, bool success, bool solver_failed, bool found_root);

        inline bool DoBraid() const { return do_braid; };
        inline bool& DoBraid() { return do_braid; };
        inline bool DoFalseVarstep() const { return do_falsevarstep; };
        inline bool& DoFalseVarstep() { return do_falsevarstep; };
        inline bool DoVarstep() const { return do_varstep; };
        inline bool& DoVarstep() { return do_varstep; };
        inline bool UseDenseMatrix() const { return dense_mat; };
        inline PVector GetPState() const { return pstate; };

        inline int GetOrder() { return order; };
        inline int GetNbStepsDone() { return nb_steps_done; };
        inline Equation* GetEq() { return equation; };
        virtual std::string GetName() { return "UnknownTI"; };
        virtual void show() = 0;
        virtual TI_type GetType() = 0;
        inline void SetRTol(Real rtol_) { rtol = rtol_; };
        inline void SetATol(const Vector& atol_) { atol = atol_; };
        inline void SetMaxRFactor(Real max_rfactor_) { max_rfactor = max_rfactor_; };
        inline Real GetMaxRFactor() { return max_rfactor; };
    };

}  // namespace paradae
}  // namespace griddyn

#endif
