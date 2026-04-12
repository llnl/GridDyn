% LLNS Copyright Start
% Copyright (c) 2017, Lawrence Livermore National Security
% This work was performed under the auspices of the U.S. Department
% of Energy by Lawrence Livermore National Laboratory in part under
% Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
% Produced at the Lawrence Livermore National Laboratory.
% All rights reserved.
% For details, see the LICENSE file.
% LLNS Copyright End
function tests = test_basic
tests = functiontests(localfunctions);
end

%% isAlmostEqual
% Test whether floating point numbers are close enough
%% Syntax
% out = isAlmostEqual(a,b)
%%
function out = isAlmostEqual(a,b,delta)
    out = isequal(a,b);
    if out==false
        if ~isfloat(a) || ~isfloat(b)
            error('isAlmostEqual:arguments', 'Arguments must float.')
        end
        a = a(:);
        b = b(:);
        % Relative difference
        difference = abs(a-b);
        relative = difference./max(abs([a b]),[],2);
        comparison = relative<delta;
        out = all(comparison);
    end
end
%%

function testFunctionOne(testCase)
% Test specific code
end

function testBasic(testCase)
global ieee_test_directory
global matlab_test_directory
global other_test_directory

    import griddyn.*

    sim = gridDynSimulation_create('', 'sim1')
    file = char(ieee_test_directory + "ieee14.cdf")
    res = gridDynSimulation_loadfile(sim, file, '')
    assert(isequal(res, griddyn.EXECUTION_SUCCESS))
    res = gridDynSimulation_run(sim)
    assert(isequal(res, griddyn.EXECUTION_SUCCESS))
    time = gridDynSimulation_getCurrentTime(sim)
    assert(isAlmostEqual(time,30.0, 0.0001))
end

function testGetResults(testCase)
   global ieee_test_directory

   import griddyn.*
   sim = gridDynSimulation_create('', 'sim2')
   if isequal(sim,0) == false
      file = char(ieee_test_directory + "ieee14.cdf")
      res = gridDynSimulation_loadfile(sim, file, '')
      assert(isequal(res, EXECUTION_SUCCESS))
      res = gridDynSimulation_powerflow(sim)
%      voltages = griddyn.doubleArray(cnt)
%      act = griddyn.gridDynSimulation_getResults(sim, "voltage" ,voltages, cnt);

   end
end

%        angles = griddyn.doubleArray(cnt)
%        self.assertEqual(cnt,act)
%        act = griddyn.gridDynSimulation_getResults(sim, "angles" ,angles, cnt);

%            for i in range(cnt):
%                print i,voltages[i]
%
%            for i in range(cnt):
%                print i,angles[i]

%        self.assertEqual(cnt,act)
%        self.assertAlmostEqual(voltages[0], 1.06, delta=0.00001);
%        self.assertAlmostEqual(angles[0], 0, delta= 0.0000001);
%        self.assertAlmostEqual(voltages[1], 1.045, delta=0.00001);
%        self.assertAlmostEqual(voltages[2], 1.01, delta=0.00001);
%        self.assertAlmostEqual(voltages[8], 1.056, delta=0.001);
%        self.assertAlmostEqual(angles[5], -14.22*3.1415927 / 180.0, delta=0.001);
%        del voltages
%        del angles
%     end
% end
%% Optional file fixtures
function setupOnce(testCase)  % do not change function name
% set a new path, for example
end

function teardownOnce(testCase)  % do not change function name
% change back to original path, for example
end

%% Optional fresh fixtures

function setup(testCase)  % do not change function name
% open a figure, for example
global ieee_test_directory
global matlab_test_directory
global other_test_directory
ieee_test_directory = '../../test/test_files/IEEE_test_cases/'
matlab_test_directory = '/matlab_test_files/'
other_test_directory = '/other_test_cases/'
end

function teardown(testCase)  % do not change function name
% close figure, for example
end
