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
function res=readDebugResidFile(filename)


fid=fopen(filename);
if (fid<=0)
    return
end

res=struct('time',[],'state',[],'resid',[],'dr',[],'ds',[],'stnames',{});
currentRes=1;
indexBias=0;
mxstate=10^50;
line=fgets(fid);
while (~isempty(line))
    if (isequal(line,-1))
        break;
    end
    t1=regexp(line,'(\d+)\[([^\]]*)\]\s(\d+)\sr=([^s]*)state=([^\s])[^=]*=([^\s])[^=]*=([^\s])','tokens');

    if (~isempty(t1))
        index=str2double(t1{1}{1})-indexBias;
        st=str2double(t1{1}{3})+1;
        if (st>mxstate)
            indexBias=length(res(currentRes).time)-1;
            currentRes=currentRes+1;
            for jj=1:mxstate
                res(currentRes).resid(1,jj)=res(currentRes-1).resid(end,jj);
                res(currentRes).state(1,jj)=res(currentRes-1).state(end,jj);
                res(currentRes).dr(1,jj)=res(currentRes-1).dr(end,jj);
                res(currentRes).ds(1,jj)=res(currentRes-1).ds(end,jj);

            end
            res(currentRes-1).state(end,:)=[];
                res(currentRes-1).time(end)=[];
                res(currentRes-1).resid(end,:)=[];
                res(currentRes-1).dr(end,:)=[];
                res(currenRes-1).ds(end,:)=[];
            index=str2double(t1{1}{1})-indexBias;
        end
        res(currentRes).time(index) = str2double(t1{1}{2});

        res(currentRes).resid(index,st)=str2double(t1{1}{4});
        res(currentRes).state(index,st)=str2double(t1{1}{5});
        res(currentRes).dr(index,st)=str2double(t1{1}{6});
        res(currentRes).ds(index,st)=str2double(t1{1}{7});
        line=fgets(fid);
        continue;
    end

    t1=regexp(line,'(\d+)\[([^\]]*)\]\s(\d+)\sr=([^s]*)state=([^\s])','tokens');
    if (~isempty(t1))
        index=str2double(t1{1}{1})-indexBias;
        st=str2double(t1{1}{3})+1;
        if (st>mxstate)
            indexBias=length(res(currentRes).time)-1;
            currentRes=currentRes+1;
            for jj=1:mxstate
                res(currentRes).resid(1,jj)=res(currentRes-1).resid(end,jj);
                res(currentRes).state(1,jj)=res(currentRes-1).state(end,jj);
            end
            res(currentRes-1).state(end,:)=[];
                res(currentRes-1).time(end)=[];
                res(currentRes-1).resid(end,:)=[];
            index=str2double(t1{1}{1})-indexBias;
            mxstate=10^50;
        end
        res(currentRes).time(index) = str2double(t1{1}{2});
        res(currentRes).resid(index,st)=str2double(t1{1}{4});
        res(currentRes).state(index,st)=str2double(t1{1}{5});
        line=fgets(fid);
        continue;
    end
    t2=regexp(line,'\[([^\]]*)\]:([^=]*)','tokens');
    if (~isempty(t2))
    while (~isempty(t2))
        st=str2double(t2{1}{1})+1;
        if (isnan(st))  % might trigger on other griddyn messages
            line=fgets(fid);
            break;
        end
        res(currentRes).stnames{st}=t2{1}{2};
        mxstate=st;
        line=fgets(fid);
        t2=regexp(line,'\[([^\]]*)\]:([^=]*)','tokens');
    end
    continue;
    end
    %stname block
    line=fgets(fid);

  %  t1=regexp(line,'(\d+)\[([^\]]*)','tokens');
end
fclose(fid);
