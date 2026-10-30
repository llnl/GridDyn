function [state, deriv, time] = processCVODEStatefile(filename, doPlot)
%PROCESSCVODESTATEFILE Extract and optionally plot GridDyn state records.
%   [STATE, DERIV, TIME] = PROCESSCVODESTATEFILE(FILENAME, DOPLOT) returns
%   one row per record and one column per state. DOPLOT defaults to true.
if nargin < 2
    doPlot = true;
end
records = readStateFile(filename);
stateIndices = find(records.typecode == 0 & records.code == 0);
derivIndices = find(records.typecode == 0 & records.code == 1);
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
