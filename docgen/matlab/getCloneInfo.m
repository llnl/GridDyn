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
function cinfo=getCloneInfo(nname,cstring)
%function get the double set information from a file
%dset=getDoubleSet(nname,cstring)
% dset contains a cell array with the extracted data
%nname is the name of the class
% cstring is the text of the file

cinfo={};
%a=regexp(cstring,['int\s*',nname,'::\s*set\s*\(\s*const std::string &param, double val, units_t unitType\)'],'tokenExtents');
a=regexp(cstring,['coreObject\s*\*\s*',nname,'::\s*clone\s*\(\s*coreObject \*\s*[^)]*)'],'end');
if (isempty(a))
  return;
end
block=extractCodeBlock(cstring,a(1)+1);

pstr=regexp(block,'[^*]*\*\s*([^=]*)=','tokens');
if (isempty(pstr))
    keyname='obj';
else
keyname=strtrim(pstr{1});
end

vnames=regexp(block,[keyname,'->([^=;]*)=([^;]*)'],'tokens');
if (numel(vnames)==2)
    for nn=1:numel(vnames{2})
        cinfo{nn,1}=strtrim(vnames{2}{nn}{1});
        cinfo{nn,2}=strtrim(vnames{2}{nn}{2});
    end
else
    for nn=1:numel(vnames)
        cinfo{nn,1}=strtrim(vnames{nn}{1});
        cinfo{nn,2}=strtrim(vnames{nn}{2});
    end;
end
