function [jdata, names] = readJacFile(filename)
%READJACFILE Read the last sparse Jacobian in a GridDyn HDF5 capture.
%   JDATA contains zero-based row and col vectors and a val vector. NAMES
%   contains state names if they were included in the file.
records = readStateFile(filename);
index = find(records.typecode == 1, 1, 'last');
if isempty(index)
    error('GridDyn:JacFileFormat', 'The file does not contain a Jacobian');
end
jdata = records.data{index};
names = {};
nameIndex = find(records.typecode == 2 & records.frame == records.frame(index) & ...
                 records.key == records.key(index), 1, 'last');
if ~isempty(nameIndex)
    names = records.data{nameIndex};
end
end
