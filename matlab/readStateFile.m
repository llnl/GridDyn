% Copyright (c) 2014-2026, Lawrence Livermore National Security
% See the top-level NOTICE for additional details. All rights reserved.
% SPDX-License-Identifier: BSD-3-Clause

function res = readStateFile(filename, varargin)
%READSTATEFILE Read GridDyn HDF5 state, diagnostic, and recorder captures.
%   RES.data contains vectors, sparse triplets (row,col,val,rows,columns),
%   or UTF-8 names. RES.typecode is 0 vector, 1 matrix, 2 names.
%   RES.source is 0 snapshot, 1 diagnostic, 2 checkpoint runtime, or 3 recorder.
%   Recorder codes: 0 samples, 1 channel names, 2 recorder name/description.
%   Diagnostic codes: 0 state, 1 derivative/RHS,
%   2 residual. Times are seconds plus exact int64 time_ns; frame, index and
%   key retain uint64 identity. Snapshot index is its frame; block_index
%   distinguishes a paired block. Matrices use zero-based uint64 coordinates.
%   Name/value selectors: Frame, Source, Key, Code. MetadataOnly=true skips
%   all payload reads. Unpublished trailing records are ignored. Old custom
%   binary files are not supported.
options = struct('Frame', [], 'Source', [], 'Key', [], 'Code', [], 'MetadataOnly', false);
if mod(numel(varargin), 2) ~= 0
    error('GridDyn:StateFileOptions', 'Expected name/value options');
end
fields = fieldnames(options);
for kk = 1:2:numel(varargin)
    match = find(strcmpi(varargin{kk}, fields), 1);
    if isempty(match)
        error('GridDyn:StateFileOptions', 'Unknown option: %s', varargin{kk});
    end
    options.(fields{match}) = varargin{kk + 1};
end
if exist(filename, 'file') ~= 2
    error('GridDyn:StateFileOpen', 'Unable to open state file: %s', filename);
end
res = struct('time', [], 'time_ns', int64([]), 'index', uint64([]), ...
    'block_index', uint64([]), 'frame', uint64([]), 'code', [], 'typecode', [], ...
    'key', uint64([]), 'paired_key', uint64([]), 'parent_index', uint64([]), ...
    'source', [], 'flags', uint32([]), 'approx', uint32([]), 'cj', [], 'data', {{}});
try
    format = h5readatt(filename, '/', 'format');
    format = format(format ~= 0);
    if ~strcmp(char(format(:).'), 'griddyn-data') || ...
            h5readatt(filename, '/', 'schema_version') ~= 1
        error('GridDyn:StateFileFormat', 'Unsupported GridDyn HDF5 schema');
    end
    committed = h5read(filename, '/committed_records');
    indexInfo = h5info(filename, '/records');
    if ~isscalar(committed) || ~isa(committed, 'uint64') || ...
            committed > uint64(indexInfo.Dataspace.Size)
        error('GridDyn:StateFileFormat', 'Invalid completed-record count');
    end
    count = exactIndex(committed);
    valueInfo = h5info(filename, '/values');
    indexDataInfo = h5info(filename, '/indices');
    byteInfo = h5info(filename, '/bytes');
    for first = 1:512:count
        n = min(512, count - first + 1);
        records = h5read(filename, '/records', first, n);
        for ii = 1:n
            if ~isempty(options.Frame) && ~any(records.frame(ii) == uint64(options.Frame)), continue; end
            if ~isempty(options.Source) && ~any(records.source(ii) == options.Source), continue; end
            if ~isempty(options.Key) && ~any(records.key(ii) == uint64(options.Key)), continue; end
            if ~isempty(options.Code) && ~any(records.code(ii) == options.Code), continue; end
            kind = double(records.type(ii));
            if ~any(kind == [0 1 2])
                error('GridDyn:StateFileFormat', 'Unsupported record kind');
            end
            payload = [];
            if ~options.MetadataOnly
                if kind == 0 || kind == 1
                    payload = readRange(filename, '/values', records.value_offset(ii), ...
                                        records.count(ii), valueInfo.Dataspace.Size);
                    if kind == 1
                        entries = records.count(ii);
                        checkRange(records.index_offset(ii), entries, indexDataInfo.Dataspace.Size);
                        columnsOffset = records.index_offset(ii) + entries;
                        row = readRange(filename, '/indices', records.index_offset(ii), entries, indexDataInfo.Dataspace.Size);
                        col = readRange(filename, '/indices', columnsOffset, entries, indexDataInfo.Dataspace.Size);
                        if any(row >= records.rows(ii)) || any(col >= records.columns(ii))
                            error('GridDyn:StateFileFormat', 'Matrix coordinate exceeds shape');
                        end
                        payload = struct('row', row, 'col', col, 'val', payload, ...
                                         'rows', records.rows(ii), 'columns', records.columns(ii));
                    end
                else
                    offsets = readRange(filename, '/indices', records.index_offset(ii), ...
                                        records.count(ii) + uint64(1), indexDataInfo.Dataspace.Size);
                    bytes = readRange(filename, '/bytes', records.byte_offset(ii), ...
                                      records.byte_count(ii), byteInfo.Dataspace.Size);
                    if isempty(offsets) || offsets(1) ~= 0 || offsets(end) ~= records.byte_count(ii) || ...
                            any(offsets(2:end) < offsets(1:end-1))
                        error('GridDyn:StateFileFormat', 'Invalid name offsets');
                    end
                    payload = cell(exactIndex(records.count(ii)), 1);
                    for jj = 1:numel(payload)
                        if offsets(jj) == offsets(jj+1)
                            payload{jj} = '';
                        else
                            payload{jj} = native2unicode(bytes(exactIndex(offsets(jj)) + 1:exactIndex(offsets(jj+1))).', 'UTF-8');
                        end
                    end
                end
            end
            at = numel(res.time) + 1;
            res.time_ns(at) = records.time_ns(ii);
            res.time(at) = double(records.time_ns(ii)) * 1e-9;
            res.frame(at) = records.frame(ii);
            res.block_index(at) = records.index(ii);
            if records.source(ii) == 0, res.index(at) = records.frame(ii);
            else, res.index(at) = records.index(ii); end
            res.key(at) = records.key(ii);
            res.paired_key(at) = records.paired_key(ii);
            res.parent_index(at) = records.parent_index(ii);
            res.typecode(at) = kind;
            res.code(at) = double(records.code(ii));
            res.source(at) = double(records.source(ii));
            res.flags(at) = records.flags(ii);
            res.approx(at) = records.approx(ii);
            res.cj(at) = records.cj(ii);
            res.data{at} = payload;
        end
    end
catch exc
    if strncmp(exc.identifier, 'GridDyn:', 8), rethrow(exc); end
    error('GridDyn:StateFileFormat', 'Unable to read GridDyn HDF5 capture: %s', exc.message);
end
end

function checkRange(offset, count, size)
if offset > uint64(size) || count > uint64(size) - offset
    error('GridDyn:StateFileFormat', 'Invalid or incomplete dataset range');
end
end

function values = readRange(filename, path, offset, count, size)
checkRange(offset, count, size);
if count == 0
    if strcmp(path, '/indices'), values = uint64([]);
    elseif strcmp(path, '/bytes'), values = uint8([]);
    else, values = []; end
else
    values = h5read(filename, path, exactIndex(offset) + 1, exactIndex(count));
    values = values(:);
end
end

function value = exactIndex(value)
if value > uint64(flintmax)
    error('GridDyn:StateFileFormat', 'Dataset selection exceeds MATLAB exact indexing range');
end
value = double(value);
end
