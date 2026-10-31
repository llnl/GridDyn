function [state, Jac, names] = readJacobianCapture(filename)
%READJACOBIANCAPTURE Read a GridDyn state and sparse Jacobian capture.
%   [STATE, JAC, NAMES] = READJACOBIANCAPTURE(FILENAME) reads the diagnostic file
%   produced by captureJacState. JAC is a sparse matrix; NAMES is optional.
records = readStateFile(filename);
matrixIndex = find(records.typecode == 1, 1, 'last');
stateIndex = [];
if ~isempty(matrixIndex)
    stateIndex = find(records.typecode == 0 & records.code == 0 & ...
                      records.frame == records.frame(matrixIndex) & ...
                      records.key == records.key(matrixIndex), 1);
end
if isempty(stateIndex) || isempty(matrixIndex)
    error('GridDyn:JacStateFormat', 'The file must contain a state and a Jacobian');
end
state = records.data{stateIndex};
matrix = records.data{matrixIndex};
Jac = sparse(double(matrix.row) + 1, double(matrix.col) + 1, matrix.val, ...
             double(matrix.rows), double(matrix.columns));
namesIndex = find(records.typecode == 2 & records.frame == records.frame(matrixIndex) & ...
                  records.key == records.key(matrixIndex), 1);
names = {};
if ~isempty(namesIndex)
    names = records.data{namesIndex};
end
end
