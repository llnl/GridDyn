function [state, Jac, names] = loadJacState(filename)
%LOADJACSTATE Use the shared MATLAB HDF5 capture reader.
addpath(fullfile(fileparts(mfilename('fullpath')), '..', '..', 'matlab'));
[state, Jac, names] = readJacobianCapture(filename);
end
