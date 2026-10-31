function [state, deriv, time] = processCVODEStatefile(filename, doPlot, key)
%PROCESSCVODESTATEFILE Extract and optionally plot GridDyn state records.
%   [STATE, DERIV, TIME] = PROCESSCVODESTATEFILE(FILENAME, DOPLOT) returns
%   one row per record and one column per state. DOPLOT defaults to true.
%   Optional KEY selects a solver; by default select the first differential
%   block so paired algebraic vectors are not mixed into the state rows.
if nargin < 2
    doPlot = true;
end
records = readStateFile(filename);
if nargin < 3
    differential = find(records.typecode == 0 & records.code == 0 & ...
                        bitand(records.flags, uint32(2)) ~= 0, 1);
    if isempty(differential), key = []; else, key = records.key(differential); end
end
if ~isempty(key)
    stateSelection = records.key == uint64(key);
else
    stateSelection = true(size(records.key));
end
stateIndices = find(stateSelection & records.typecode == 0 & records.code == 0);
derivIndices = find(stateSelection & records.typecode == 0 & records.code == 1);
state = vectorsToRows(records.data, stateIndices);
deriv = vectorsToRows(records.data, derivIndices);
time = records.time(stateIndices);
if doPlot
    for kk = 1:size(state, 2)
        figure(1);
        plot(time, state(:, kk));
        title(sprintf('state %d', kk));
        if size(deriv, 2) >= kk
            figure(2);
            plot(records.time(derivIndices), deriv(:, kk));
            title(sprintf('state derivative %d', kk));
        end
        pause;
    end
end
end

function values = vectorsToRows(data, indices)
if isempty(indices)
    values = [];
    return;
end
count = numel(data{indices(1)});
values = zeros(numel(indices), count);
for kk = 1:numel(indices)
    vector = data{indices(kk)};
    if numel(vector) ~= count
        error('GridDyn:StateFileFormat', 'State vector lengths differ between records');
    end
    values(kk, :) = vector(:).';
end
end
