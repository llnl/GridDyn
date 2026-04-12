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
function block=extractCodeBlock(cstring,start)
%function to extract a code block from a section of C++ code
%block=extractCodeBlock(cstring,start)
% block is the output block
% cstring is the input string
% start is the location where to start looking for an extraction point

brackets=1;
cloc=start;
while (cstring(cloc)~='{')
    cloc=cloc+1;
end
startloc=cloc+1;
cloc=cloc+1;
while (brackets>0)
    if (cstring(cloc)=='}')
        brackets=brackets-1;
    end
    if (cstring(cloc)=='{')
        brackets=brackets+1;
    end
    cloc=cloc+1;
    if (cloc>length(cstring))
        break;
    end
end
block=cstring(startloc:cloc-2);
