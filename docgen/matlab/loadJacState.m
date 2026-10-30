function [state, Jac, names] = loadJacState(filename)
%LOADJACSTATE Read a GridDyn state and sparse Jacobian capture.
%   [STATE, JAC, NAMES] = LOADJACSTATE(FILENAME) reads the diagnostic file
%   produced by captureJacState. JAC is a sparse matrix; NAMES is optional.
if exist('readStateFile', 'file') ~= 2
    repo = fileparts(fileparts(fileparts(mfilename('fullpath'))));
    addpath(fullfile(repo, 'matlab'));
end
records = readStateFile(filename);
stateIndex = find(records.typecode == 0 & records.code == 0, 1);
matrixIndex = find(records.typecode == 1, 1);
if isempty(stateIndex) || isempty(matrixIndex)
    error('GridDyn:JacStateFormat', 'The file must contain a state and a Jacobian');
end
state = records.data{stateIndex};
matrix = records.data{matrixIndex};
n = numel(state);
Jac = sparse(double(matrix.row) + 1, double(matrix.col) + 1, matrix.val, n, n);
namesIndex = find(records.typecode == 2, 1);
names = {};
if ~isempty(namesIndex)
    names = records.data{namesIndex};
end
end
