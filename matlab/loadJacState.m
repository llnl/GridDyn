function [state, Jac, names] = loadJacState(filename)
%LOADJACSTATE Read an associated state, sparse Jacobian and names from HDF5.
[state, Jac, names] = readJacobianCapture(filename);
end
