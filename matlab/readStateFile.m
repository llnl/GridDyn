%% -*- Mode:matlab; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*- */
%
% LLNS Copyright Start
% Copyright (c) 2016, Lawrence Livermore National Security
% This work was performed under the auspices of the U.S. Department
% of Energy by Lawrence Livermore National Laboratory in part under
% Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
% Produced at the Lawrence Livermore National Laboratory.
% All rights reserved.
% For details, see the LICENSE file.
% LLNS Copyright End


function res=readStateFile(filename)
% res=readStateFile(filename)
% function reads a statefile generated from GridDyn for debugging purposes.
% the statefile has a number of different block types that can be read
% including states, derivatives, residuals, and jacobians.  Also To be
% included is state name information.(not working yet).
% res is  a structure array with the fields
% time, index, code, typecode, key, and data
% time contains the time of the data block
% index is an index number
% code is a code describing the data (0 for state, 1 for deriv, 2 for
% resid)  others are not defined yet
% typecode defines the type of the data (0 vector, 1 jacobian, 2, state
% names
% key enumerates the source (the solver index in griddyn)
% data is a cell array of the data

fid=fopen(filename);
if (fid<=0)
    return
end

res=struct('time',0,'index',0,'code',0,'typecode',0,'key',0,'data',{});
res(1).time=0;
moredata=true;
ind=1;

codemask=hex2dec('0000FFFF');
while (moredata)
    time=fread(fid,1,'double');
    if (isempty(time))
        moredata=false;
        continue;
    end
    datav=fread(fid,4,'uint32=>uint32');
    res.time(ind)=time;
    res.index(ind)=double(datav(2));
    res.code(ind)=double(bitand(datav(1),codemask));
    typecode=bitshift(datav(1),-16);
    res.typecode(ind)=typecode;
    res.key(ind)=double(datav(3));

    count=datav(4);

    switch(typecode)
        case {0} %get vector information
            res.data{ind}=fread(fid,count,'double');
        case {1,65535} %get jacobian information
            loc=ftell(fid);
            row=fread(fid,count,'uint32',12);
            fseek(fid,loc+4,-1);
            col=fread(fid,count,'uint32',12);
            fseek(fid,loc+8,-1);
            val=fread(fid,count,'double',8);
            fseek(fid,loc+count*16,-1);
            res.data{ind}.row=row;
            res.data{ind}.col=col;
            res.data{ind}.val=val;
        case 2 % get state name information (not described yet)
    end
    ind=ind+1;

end
fclose(fid);
