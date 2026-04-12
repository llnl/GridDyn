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
function vArray=getClassVariables(cblock)
%function to extract the class variables from a code Block

vtypes={'double','coreTime','int','index_t','count_t','std::string'};
vArray=cell(0,4);
for vv=1:length(vtypes)
    %get variables with comments

    dvar=regexp(cblock,[vtypes{vv},'\s([\w]*)\s*=([^;]*);\s*//!<([^\n]*)'],'tokens');

    for aa=1:length(dvar)
        val=str2double(dvar{aa}{2});
        if (isnan(val))
            str=strtrim(dvar{aa}{2});
           if (~checkBalancedParen(str))
               continue;
           end
           k=strfind(str,'flag');
           if(~isempty(k))
               continue;
           end
           val=transString(str,dvar{aa}{1});

        end
        vArray{end+1,1}=strtrim(dvar{aa}{1});
        vArray{end,2}=val;
        vArray{end,3}=dvar{aa}{3};
        vArray{end,4}=vtypes{vv};
    end

    %get variables with comments

    dvar=regexp(cblock,[vtypes{vv},'\s([\w]*)\s*;\s*//!<([^\n]*)'],'tokens');

    for aa=1:length(dvar)
        vArray{end+1,1}=strtrim(dvar{aa}{1});
        vArray{end,3}=dvar{aa}{2};
        vArray{end,4}=vtypes{vv};
    end

    %get variables with no comment
    dvar=regexp(cblock,[vtypes{vv},'\s([\w]*)\s*=([^;$#]*);'],'tokens');
    csize=size(vArray,1);
    for aa=1:length(dvar)
        found=0;
        for nn=1:csize
            if (isequal(dvar{aa}{1},vArray{nn,1}))
                found=1;
                break;
            end
        end
        if (found==1)
            continue;
        end
        val=str2double(dvar{aa}{2});
        if (isnan(val))
            str=strtrim(dvar{aa}{2});
            if (~checkBalancedParen(str))
               continue;
            end
           k=strfind(str,'flag');
           if(~isempty(k))
               continue;
           end
           val=transString(str,dvar{aa}{1});
        end
        vArray{end+1,1}=dvar{aa}{1};
        vArray{end,2}=val;
        vArray{end,4}=vtypes{vv};
    end
end
end


function bal=checkBalancedParen(str)
a=sum(str=='(');
b=sum(str==')');
bal=(a==b);
end

function val=transString(str,name)
switch(str)
    case {'timeZero'}
        val=0;
    case {'negTime'}
        val=-1e48;
    case {'maxTime'}
        val=1e48;
    case {'timeOne'}
        val=1.0;
    case {'kWS'}
        val=2*pi*60.0;
    case {'kBigNum'}
        val=1e48;
    case {'-kBigNum'}
        val=-1e48;
    case {'GD_SUMMARY_PRINT'}
        val=3;
    case {'GS_NO_ERROR'}
        val=0;
    case {'kNullVal','kNullLocation','kInvalidLocation','kInvalidCount'}
        val=nan;
    otherwise
        if (isequal(str(1:8),'coreTime'))
            try
            val=eval(str(10:end-1));
            catch
                fprintf('%s str=%s\n',name,str);
                val='';
            end
        else
        try
            kPI=pi;
            val=eval(str);
        catch
            fprintf('%s str=%s\n',name,str);
            val='';
        end
        end
end
end
