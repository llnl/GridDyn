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
function dset=getSetInfo(nname,cstring)
%function get the double set information from a file
%dset=getDoubleSet(nname,cstring)
% dset contains a cell array with the extracted data
%nname is the name of the class
% cstring is the text of the file

dset=cell(0,3);
%a=regexp(cstring,['int\s*',nname,'::\s*set\s*\(\s*const std::string &param, double val, units_t unitType\)'],'tokenExtents');
a=regexp(cstring,['void\s*',nname,'::\s*set\s*\(\s*const std::string &param,\s*double val,\s*units_t unitType\)'],'end');
if (isempty(a))
    a=regexp(cstring,['void\s*',nname,'::\s*set\s*\(\s*const std::string &param,\s*double val,\s*gridUnits::units_t unitType\)'],'end');
    if (isempty(a))
        fprintf('no set function for %s\n',nname);
    end
end
block=extractCodeBlock(cstring,a(1)+1);

pstr=regexp(block,'param\s*==\s*"([^"]*)','tokenExtents');
for nn=1:length(pstr)
    eblock=extractCodeBlock(block,pstr{nn}(2));
    dset{nn,1}=strtrim(block(pstr{nn}(1):pstr{nn}(2)));
    dset{nn,2}=findSettingVariable(eblock);
    dset{nn,3}='number';
end


a=regexp(cstring,['void\s*',nname,'::\s*set\s*\(\s*const std::string &param,\s*const std::string &[^\)]*\)'],'end');

block=extractCodeBlock(cstring,a(1)+1);

pstr=regexp(block,'param\s*==\s*"([^"]*)','tokenExtents');
for nn=1:length(pstr)
    eblock=extractCodeBlock(block,pstr{nn}(2));
    dset{end+1,1}=strtrim(block(pstr{nn}(1):pstr{nn}(2)));
    dset{end,2}=findSettingVariable(eblock);
    dset{end,3}='string';
end

a=regexp(cstring,['void\s*',nname,'::\s*setFlag\s*\([^\)]*)'],'end');
if (~isempty(a))
    block=extractCodeBlock(cstring,a(1)+1);

    pstr=regexp(block,'param\s*==\s*"([^"]*)','tokenExtents');
    for nn=1:length(pstr)
        eblock=extractCodeBlock(block,pstr{nn}(2));
        dset{end+1,1}=strtrim(block(pstr{nn}(1):pstr{nn}(2)));
        dset{end,2}=findSettingVariable(eblock);
        dset{end,3}='flag';
    end
    pstr=regexp(block,'flag\s*==\s*"([^"]*)','tokenExtents');
    for nn=1:length(pstr)
        eblock=extractCodeBlock(block,pstr{nn}(2));
        dset{end+1,1}=strtrim(block(pstr{nn}(1):pstr{nn}(2)));
        dset{end,2}=findSettingVariable(eblock);
        dset{end,3}='flag';
    end
end
end

function var=findSettingVariable(subBlock)

vstr=regexp(subBlock,'\s*([^=\s]*)\s*=\s*unitConversion','tokens');
if (~isempty(vstr))
    var=strtrim(vstr{1}{1});
else
    vstr=regexp(subBlock,'\s*([^=\s]*)\s*=\s*val','tokens');
    if (~isempty(vstr))
        var=strtrim(vstr{1}{1});
    else
        vstr=regexp(subBlock,'\s*([^=\s]*)\s*=\s*gridUnits::unitConversion','tokens');
        if (~isempty(vstr))
            var=strtrim(vstr{1}{1});
        else
            var='';
        end
    end
end
end
