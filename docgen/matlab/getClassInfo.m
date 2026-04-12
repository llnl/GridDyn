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
function cls=getClassInfo(filename)

%tfile='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\loadModels\otherLoads.h';

fid=fopen(filename,'r');
c=fread(fid,'char=>char');
fclose(fid);
c=c';


a=regexp(c,'\nclass ([^:*\/;{]*):\s*public ([^\s]*)','tokenExtents');
cls=struct('name','','parent','','variables',[]);
for pp=1:length(a)
    cls(pp).name=strtrim(c(a{pp}(1,1):a{pp}(1,2)));
    cls(pp).parent=strtrim(c(a{pp}(2,1):a{pp}(2,2)));
    cblock=extractCodeBlock(c,a{pp}(2,2)+1);
    vblk=getClassVariables(cblock);
    cls(pp).variables=vblk;
end

a=regexp(c,'\nclass coreObject\s*{','end');

if (~isempty(a))
    cls(end+1).name='coreObject';
    cls(end).parent='';
    cblock=extractCodeBlock(c,a(1));
    vblk=getClassVariables(cblock);
    cls(end).variables=vblk;
end
end
