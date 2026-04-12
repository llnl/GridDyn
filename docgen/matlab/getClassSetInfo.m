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
function cls=getClassSetInfo(filename)

%tfile='C:\Users\top1\Documents\codeProjects\transmission\gridDyn\loadModels\otherLoads.h';

fid=fopen(filename,'r');
c=fread(fid,'char=>char');
fclose(fid);
c=c';

%a=regexp(c,'class ([^:]*):\s*class\s*([^\s]*)','tokenExtents');

a=regexp(c,'void ([^:{}\(\),\s]*)\s*::set\s*\(','tokens');
cls=struct('name','','dset',{},'flagset',{},'stringSet',{});
clscnt=1;
for pp=1:length(a)
    name=strtrim(a{pp}{1});
    found=0;
   for cc=1:clscnt-1
       if (isequal(name,cls(cc).name))
           found=1;
           break;
       end
   end
   if (found==1)
       continue;
   end
   cls(clscnt).name=name;
    cls(clscnt).dset=getSetInfo(name,c);
    clscnt=clscnt+1;
end


end
