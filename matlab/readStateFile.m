% Copyright (c) 2014-2026, Lawrence Livermore National Security
% See the top-level NOTICE for additional details. All rights reserved.
% SPDX-License-Identifier: BSD-3-Clause

function res = readStateFile(filename)
%READSTATEFILE Read GridDyn binary state snapshots and diagnostic records.
%   RES.data contains vectors, sparse Jacobian triplets or state names.
%   RES.typecode is 0 for a vector, 1 for a matrix, and 2 for names.
%   RES.code is 0 for state, 1 for derivatives, and 2 for residuals.
%   RES.source is 0 for snapshots and 1 for solver diagnostics. RES.index
%   is a zero-based snapshot number or a diagnostic iteration index.
%   RES.time is seconds; RES.time_ns is exact int64 nanoseconds. RES.key is
%   the solver offset index; RES.flags and RES.approx describe its mode.

fid = fopen(filename, 'rb', 'ieee-le');
if fid < 0
    error('GridDyn:StateFileOpen', 'Unable to open state file: %s', filename);
end
cleanup = onCleanup(@() fclose(fid)); %#ok<NASGU>

fseek(fid, 0, 'eof');
fileSize = ftell(fid);
fseek(fid, 0, 'bof');
magic = fread(fid, 8, '*uint8');
if numel(magic) ~= 8 || ~isequal(char(magic.'), 'GDSTATE1')
    error('GridDyn:StateFileFormat', 'Unsupported state file format or version');
end

res.time = [];
res.time_ns = int64([]);
res.index = [];
res.code = [];
res.typecode = [];
res.key = [];
res.source = [];
res.flags = uint32([]);
res.approx = uint32([]);
res.data = {};
snapshot = 0;
marker = uint32(hex2dec('31544153'));
endMarker = uint32(hex2dec('31444e45'));

while ftell(fid) < fileSize
    if fileSize - ftell(fid) < 52
        error('GridDyn:StateFileFormat', 'Truncated state snapshot header or trailer');
    end
    if readRequired(fid, 1, 'uint32') ~= marker
        error('GridDyn:StateFileFormat', 'Invalid state snapshot marker');
    end
    source = readRequired(fid, 1, 'uint32');
    type = readRequired(fid, 1, 'uint32');
    code = readRequired(fid, 1, 'uint32');
    index = readRequired(fid, 1, 'uint32');
    if ~((source == 0 && type == 0) || ...
            (source == 1 && (type == 1 || type == 2 || type == 3)))
        error('GridDyn:StateFileFormat', 'Unsupported record type or source');
    end
    flags = readRequired(fid, 1, 'uint32');
    if bitand(flags, uint32(hex2dec('ffffffc0'))) ~= 0
        error('GridDyn:StateFileFormat', 'Unsupported solver mode flags');
    end
    approx = readRequired(fid, 1, 'uint32');
    key = readRequired(fid, 1, 'uint32');
    time_ns = readRequired(fid, 1, 'int64');
    count = readRequired(fid, 1, 'uint64');
    differential = bitand(flags, uint32(2)) ~= 0;
    if type == 3
        bytesPerValue = 1;
    elseif type == 2 || (type == 0 && differential)
        bytesPerValue = 16;
    else
        bytesPerValue = 8;
    end
    remaining = fileSize - ftell(fid);
    if remaining < 4 || count > uint64(floor((remaining - 4) / bytesPerValue))
        error('GridDyn:StateFileFormat', 'Invalid or truncated state vector');
    end
    count = double(count);
    if type == 0 || type == 1
        data = readRequired(fid, count, 'double');
        if type == 0 && differential
            derivative = readRequired(fid, count, 'double');
        end
    elseif type == 2
        position = ftell(fid);
        data = struct();
        data.row = fread(fid, count, 'uint32=>uint32', 12);
        fseek(fid, position + 4, 'bof');
        data.col = fread(fid, count, 'uint32=>uint32', 12);
        fseek(fid, position + 8, 'bof');
        data.val = fread(fid, count, 'double=>double', 8);
        if numel(data.row) ~= count || numel(data.col) ~= count || numel(data.val) ~= count
            error('GridDyn:StateFileFormat', 'Truncated matrix record');
        end
        fseek(fid, position + count * 16, 'bof');
    else
        data = readNames(fid, count);
    end
    if readRequired(fid, 1, 'uint32') ~= endMarker
        error('GridDyn:StateFileFormat', 'Invalid state snapshot trailer');
    end
    if type == 0
        res = addRecord(res, snapshot, 0, 0, source, key, flags, approx, time_ns, data);
        if differential
            res = addRecord(res, snapshot, 1, 0, source, key, flags, approx, time_ns, derivative);
        end
        snapshot = snapshot + 1;
    else
        res = addRecord(res, double(index), double(code), double(type - 1), source, ...
                        key, flags, approx, time_ns, data);
    end
end
if isempty(res.data)
    error('GridDyn:StateFileFormat', 'State file contains no records');
end
end

function data = readRequired(fid, count, datatype)
data = fread(fid, count, [datatype '=>' datatype]);
if numel(data) ~= count
    error('GridDyn:StateFileFormat', 'Truncated state file');
end
end

function data = readNames(fid, bytes)
start = ftell(fid);
if bytes < 4
    error('GridDyn:StateFileFormat', 'Invalid state name record');
end
count = double(readRequired(fid, 1, 'uint32'));
if count > floor((bytes - 4) / 4)
    error('GridDyn:StateFileFormat', 'Invalid state name count');
end
data = cell(count, 1);
for kk = 1:count
    if ftell(fid) + 4 > start + bytes
        error('GridDyn:StateFileFormat', 'Truncated state name');
    end
    length = double(readRequired(fid, 1, 'uint32'));
    if length > start + bytes - ftell(fid)
        error('GridDyn:StateFileFormat', 'Invalid state name length');
    end
    data{kk} = native2unicode(readRequired(fid, length, 'uint8').', 'UTF-8');
end
if ftell(fid) ~= start + bytes
    error('GridDyn:StateFileFormat', 'Invalid state name record length');
end
end

function res = addRecord(res, index, code, typecode, source, key, flags, approx, time_ns, data)
res.time(end + 1) = double(time_ns) * 1e-9;
res.time_ns(end + 1) = time_ns;
res.index(end + 1) = index;
res.code(end + 1) = code;
res.typecode(end + 1) = typecode;
res.source(end + 1) = double(source);
res.key(end + 1) = double(key);
res.flags(end + 1) = flags;
res.approx(end + 1) = approx;
res.data{end + 1} = data;
end
