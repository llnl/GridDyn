%% -*- Mode:matlab; c-file-style:"gnu"; indent-tabs-mode:nil;  eval: (c-set-offset 'innamespace 0); -*- */
%
% LLNS Copyright Start
% Copyright (c) 2017, Lawrence Livermore National Security
% This work was performed under the auspices of the U.S. Department
% of Energy by Lawrence Livermore National Laboratory in part under
% Contract W-7405-Eng-48 and in part under Contract DE-AC52-07NA27344.
% Produced at the Lawrence Livermore National Laboratory.
% All rights reserved.
% For details, see the LICENSE file.
% LLNS Copyright End

%function to compare cloning functions
function cloneCompare(cls,cln)

for ii=1:size(cls.variables,1)
    vname=cls.variables{ii,1};
    fnd=false;
    for pp=1:size(cln.cinfo,1)
        if (isequal(vname,cln.cinfo{pp,1}))
            fnd=true;
            if (~isequal(cln.cinfo{pp,2},vname))
                fprintf('class %s field %s not matching %s\n',cls.name,vname,cln.cinfo{pp,2});
            end
            break;
        end
    end
    if (fnd==false)
        cloc=strfind(cls.name,'Count');  % to ignore counters
        if (isempty(cloc))
           fprintf('class %s field %s not found in clone\n',cls.name,vname);
        end
    end
end
